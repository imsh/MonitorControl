#include "Commands.h"

#include "VcpMath.h"

namespace
{
constexpr wchar_t kNoMonitors[] = L"No DDC/CI monitors found.\r\n";

CommandResult List(std::vector<DdcMonitor>& monitors)
{
    if (monitors.empty())
        return { .exitCode = 1, .output = kNoMonitors };

    CommandResult result;
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        DdcMonitor& monitor = monitors[i];
        // Fresh values: the monitor's own buttons may have changed them.
        ReadVcp(monitor.physical, kVcpBrightness, monitor.brightness);
        std::wstring contrast = L"n/a";
        if (monitor.hasContrast)
        {
            ReadVcp(monitor.physical, kVcpContrast, monitor.contrast);
            contrast = std::to_wstring(RawToPercent(monitor.contrast.current, monitor.contrast.max)) + L"%";
        }
        result.output += std::to_wstring(i + 1) + L"  " + monitor.name + L"  brightness " +
                         std::to_wstring(RawToPercent(monitor.brightness.current, monitor.brightness.max)) +
                         L"%  contrast " + contrast + L"\r\n";
    }
    return result;
}

CommandResult Apply(std::vector<DdcMonitor>& monitors, const Command& command)
{
    std::vector<std::wstring> names;
    for (const DdcMonitor& monitor : monitors)
        names.push_back(monitor.name);
    const std::vector<size_t> selected = SelectMonitors(command.monitor, names);
    if (monitors.empty())
        return { .exitCode = 1, .output = kNoMonitors };
    if (selected.empty())
        return { .exitCode = 1, .output = L"No monitor matches '" + command.monitor + L"'. Use --list to see them.\r\n" };

    CommandResult result;
    for (size_t index : selected)
    {
        DdcMonitor& monitor = monitors[index];
        std::wstring line = monitor.name + L" (" + std::to_wstring(index + 1) + L"):";
        for (const Change& change : command.changes)
        {
            const bool contrast = change.code == kVcpContrast;
            const wchar_t* feature = contrast ? L"contrast" : L"brightness";
            if (contrast && !monitor.hasContrast)
            {
                line += L" contrast not supported";
                result.exitCode = 1;
                continue;
            }

            VcpValue& value = contrast ? monitor.contrast : monitor.brightness;
            if (change.relative && !ReadVcp(monitor.physical, change.code, value))
            {
                line += std::wstring(L" ") + feature + L" read failed";
                result.exitCode = 1;
                continue;
            }
            const int percent = ResolvePercent(RawToPercent(value.current, value.max), change);
            const DWORD raw = PercentToRaw(percent, value.max);
            if (!WriteVcp(monitor.physical, change.code, raw))
            {
                line += std::wstring(L" ") + feature + L" write failed";
                result.exitCode = 1;
                continue;
            }
            value.current = raw;
            line += std::wstring(L" ") + feature + L" " + std::to_wstring(percent) + L"%";
        }
        result.output += line + L"\r\n";
    }
    return result;
}
} // namespace

CommandResult RunCommand(std::vector<DdcMonitor>& monitors, const Command& command)
{
    switch (command.action)
    {
    case Command::Action::List:
        return List(monitors);
    case Command::Action::Apply:
        return Apply(monitors, command);
    default:
        return { .exitCode = 2, .output = UsageText() };
    }
}
