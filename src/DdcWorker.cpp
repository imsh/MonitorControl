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

void DdcWorker::Run()
{
    for (;;)
    {
        bool enumerate = false;
        bool read = false;
        PendingWrites::Map writes;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || enumerate_ || read_ || !pending_.Empty(); });
            if (stop_)
                break;
            enumerate = std::exchange(enumerate_, false);
            read = std::exchange(read_, false);
            writes = pending_.TakeAll();
        }

        if (enumerate)
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
            continue;
        }

        for (const auto& [key, percent] : writes)
        {
            if (key.monitor >= monitors_.size())
                continue;
            DdcMonitor& monitor = monitors_[key.monitor];
            VcpValue& value = key.code == kVcpContrast ? monitor.contrast : monitor.brightness;
            const DWORD raw = PercentToRaw(percent, value.max);
            if (WriteVcp(monitor.physical, key.code, raw))
                value.current = raw;
            else
                Log(L"Setting VCP " + std::to_wstring(key.code) + L" failed on " + monitor.name);
        }

        if (read)
        {
            for (DdcMonitor& monitor : monitors_)
            {
                ReadVcp(monitor.physical, kVcpBrightness, monitor.brightness);
                if (monitor.hasContrast)
                    ReadVcp(monitor.physical, kVcpContrast, monitor.contrast);
            }
            Publish();
        }
    }
    DestroyDdcMonitors(monitors_);
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
