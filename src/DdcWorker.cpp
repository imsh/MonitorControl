#include "DdcWorker.h"

#include <utility>

#include "Log.h"
#include "VcpMath.h"

DdcWorker::DdcWorker(SnapshotCallback onSnapshot)
    : onSnapshot_(std::move(onSnapshot))
{
}

DdcWorker::~DdcWorker()
{
    Stop();
}

void DdcWorker::Start()
{
    thread_ = std::thread([this] { Run(); });
}

void DdcWorker::Stop()
{
    {
        std::scoped_lock lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void DdcWorker::RequestEnumerate()
{
    {
        std::scoped_lock lock(mutex_);
        enumerate_ = true;
    }
    wake_.notify_one();
}

void DdcWorker::RequestRead()
{
    {
        std::scoped_lock lock(mutex_);
        read_ = true;
    }
    wake_.notify_one();
}

void DdcWorker::Set(uint64_t generation, size_t monitor, BYTE code, int percent)
{
    {
        std::scoped_lock lock(mutex_);
        if (generation != generation_)
            return;
        pending_.Set({ .monitor = monitor, .code = code }, percent);
    }
    wake_.notify_one();
}

CommandResult DdcWorker::RunCommand(const Command& command)
{
    std::future<CommandResult> result;
    {
        std::scoped_lock lock(mutex_);
        if (stop_)
            return { .exitCode = 1, .output = L"Monitor Control is shutting down.\r\n" };
        commands_.push_back({ .command = command });
        result = commands_.back().result.get_future();
    }
    wake_.notify_one();
    return result.get();
}

void DdcWorker::Run()
{
    for (;;)
    {
        bool enumerate = false;
        bool read = false;
        PendingWrites::Map writes;
        std::vector<QueuedCommand> commands;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] {
                return stop_ || enumerate_ || read_ || !pending_.Empty() || !commands_.empty();
            });
            if (stop_)
                break;
            enumerate = std::exchange(enumerate_, false);
            read = std::exchange(read_, false);
            writes = pending_.TakeAll();
            commands = std::exchange(commands_, {});
        }

        if (enumerate)
        {
            Enumerate();
            writes.clear();
        }

        // Physical monitor handles go stale when a monitor drops off the bus and comes back (e.g. it
        // went to standby) without Windows telling us. Enumerate again once and retry.
        std::vector<std::pair<WriteKey, int>> failed;
        for (const auto& [key, percent] : writes)
        {
            if (!Write(key, percent))
                failed.emplace_back(key, percent);
        }
        if (!failed.empty())
        {
            std::vector<std::wstring> oldNames;
            for (const DdcMonitor& monitor : monitors_)
                oldNames.push_back(monitor.name);
            Log(L"Writing failed, enumerating the monitors again");
            Enumerate();

            for (const auto& [key, percent] : failed)
            {
                // Only retry if the index still points at the same monitor.
                if (key.monitor < monitors_.size() && key.monitor < oldNames.size() &&
                    monitors_[key.monitor].name == oldNames[key.monitor] && Write(key, percent))
                    continue;
                const std::wstring name = key.monitor < oldNames.size() ? oldNames[key.monitor] : L"?";
                Log(L"Setting VCP " + std::to_wstring(key.code) + L" failed on " + name);
            }
            Publish();
        }

        for (QueuedCommand& queued : commands)
            queued.result.set_value(::RunCommand(monitors_, queued.command));

        if (read)
        {
            for (DdcMonitor& monitor : monitors_)
            {
                ReadVcp(monitor.physical, kVcpBrightness, monitor.brightness);
                if (monitor.hasContrast)
                    ReadVcp(monitor.physical, kVcpContrast, monitor.contrast);
            }
        }
        if (read || !commands.empty())
            Publish();
    }

    {
        // Nobody may be left waiting in RunCommand().
        std::scoped_lock lock(mutex_);
        for (QueuedCommand& queued : commands_)
            queued.result.set_value({ .exitCode = 1, .output = L"Monitor Control is shutting down.\r\n" });
        commands_.clear();
    }
    DestroyDdcMonitors(monitors_);
}

void DdcWorker::Enumerate()
{
    DestroyDdcMonitors(monitors_);
    monitors_ = EnumerateDdcMonitors();

    std::wstring names;
    for (const DdcMonitor& monitor : monitors_)
        names += L"\n    " + monitor.name + (monitor.hasContrast ? L"" : L" (no contrast)");
    Log(L"Found " + std::to_wstring(monitors_.size()) + L" DDC/CI monitor(s)" + names);

    {
        // Writes queued against the old list are meaningless now.
        std::scoped_lock lock(mutex_);
        ++generation_;
        pending_.TakeAll();
    }
    Publish();
}

bool DdcWorker::Write(const WriteKey& key, int percent)
{
    if (key.monitor >= monitors_.size())
        return true;   // nothing to write to
    DdcMonitor& monitor = monitors_[key.monitor];
    VcpValue& value = key.code == kVcpContrast ? monitor.contrast : monitor.brightness;
    const DWORD raw = PercentToRaw(percent, value.max);
    if (!WriteVcp(monitor.physical, key.code, raw))
        return false;
    value.current = raw;
    return true;
}

void DdcWorker::Publish() const
{
    DdcSnapshot snapshot{ .generation = generation_ };
    for (const DdcMonitor& monitor : monitors_)
    {
        snapshot.monitors.push_back({
            .name = monitor.name,
            .brightness = RawToPercent(monitor.brightness.current, monitor.brightness.max),
            .hasContrast = monitor.hasContrast,
            .contrast = RawToPercent(monitor.contrast.current, monitor.contrast.max),
        });
    }
    onSnapshot_(std::move(snapshot));
}
