#include "CommandLine.h"

#include <algorithm>
#include <cwctype>
#include <initializer_list>

#include "VcpMath.h"

namespace
{
bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

bool IsOneOf(const std::wstring& arg, std::initializer_list<const wchar_t*> names)
{
    return std::any_of(names.begin(), names.end(), [&](const wchar_t* name) { return EqualsIgnoreCase(arg, name); });
}

bool IsNumber(std::wstring_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// "N" (0..100), or "+N" / "-N" (a step of at most 100).
bool ParseValue(std::wstring_view text, Change& change)
{
    change.relative = !text.empty() && (text.front() == L'+' || text.front() == L'-');
    const bool negative = change.relative && text.front() == L'-';
    const std::wstring_view digits = change.relative ? text.substr(1) : text;
    if (!IsNumber(digits) || digits.size() > 3)
        return false;
    const int value = std::stoi(std::wstring(digits));
    if (value > 100)
        return false;
    change.value = negative ? -value : value;
    return true;
}
} // namespace

ParseResult ParseCommandLine(const std::vector<std::wstring>& args)
{
    ParseResult result;
    Command& command = result.command;
    if (args.empty())
        return result;

    bool help = false;
    bool list = false;
    bool hasMonitor = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring& arg = args[i];
        if (IsOneOf(arg, { L"--help", L"-h", L"-?", L"/?" }))
        {
            help = true;
            continue;
        }
        if (IsOneOf(arg, { L"--list", L"-l" }))
        {
            list = true;
            continue;
        }

        const bool monitor = IsOneOf(arg, { L"--monitor", L"-m" });
        const bool brightness = IsOneOf(arg, { L"--brightness", L"-b" });
        const bool contrast = IsOneOf(arg, { L"--contrast", L"-c" });
        if (!monitor && !brightness && !contrast)
        {
            result.error = L"Unknown option: " + arg;
            return result;
        }
        if (i + 1 >= args.size() || args[i + 1].empty())
        {
            result.error = arg + L" needs a value";
            return result;
        }
        const std::wstring& value = args[++i];

        if (monitor)
        {
            if (hasMonitor)
            {
                result.error = L"--monitor is given more than once";
                return result;
            }
            hasMonitor = true;
            command.monitor = value;
            continue;
        }

        Change change{ .code = brightness ? kVcpBrightness : kVcpContrast };
        if (std::any_of(command.changes.begin(), command.changes.end(),
                        [&](const Change& existing) { return existing.code == change.code; }))
        {
            result.error = std::wstring(brightness ? L"--brightness" : L"--contrast") + L" is given more than once";
            return result;
        }
        if (!ParseValue(value, change))
        {
            result.error = L"Invalid value for " + arg + L": " + value + L" (use 0..100, +N or -N)";
            return result;
        }
        command.changes.push_back(change);
    }

    if (help)
    {
        command.action = Command::Action::Help;
    }
    else if (list)
    {
        if (hasMonitor || !command.changes.empty())
            result.error = L"--list can't be combined with other options";
        command.action = Command::Action::List;
    }
    else if (command.changes.empty())
    {
        result.error = L"--monitor needs --brightness or --contrast";
    }
    else
    {
        command.action = Command::Action::Apply;
    }
    return result;
}

std::vector<size_t> SelectMonitors(const std::wstring& selector, const std::vector<std::wstring>& names)
{
    std::vector<size_t> selected;
    if (selector.empty())
    {
        for (size_t i = 0; i < names.size(); ++i)
            selected.push_back(i);
        return selected;
    }
    if (IsNumber(selector))
    {
        if (selector.size() <= 3)
        {
            const size_t number = std::stoul(selector);
            if (number >= 1 && number <= names.size())
                selected.push_back(number - 1);
        }
        return selected;
    }
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (EqualsIgnoreCase(names[i], selector))
            selected.push_back(i);
    }
    return selected;
}

int ResolvePercent(int current, const Change& change)
{
    return std::clamp(change.relative ? current + change.value : change.value, 0, 100);
}

std::wstring UsageText()
{
    return L"Usage: MonitorControl.exe [options]\r\n"
           L"\r\n"
           L"Without options, Monitor Control runs in the system tray.\r\n"
           L"\r\n"
           L"  -b, --brightness VALUE   set brightness: 0..100, or +N / -N to step it\r\n"
           L"  -c, --contrast VALUE     set contrast:   0..100, or +N / -N to step it\r\n"
           L"  -m, --monitor MONITOR    only this monitor: its number (1 = leftmost) or its name;\r\n"
           L"                           without it the change goes to every monitor\r\n"
           L"  -l, --list               list the monitors with their current values\r\n"
           L"  -h, --help               show this help\r\n"
           L"\r\n"
           L"If Monitor Control is running in the tray, the change is made by it and its sliders\r\n"
           L"follow; otherwise the change is made directly.\r\n"
           L"\r\n"
           L"Examples:\r\n"
           L"  MonitorControl.exe --brightness 40\r\n"
           L"  MonitorControl.exe --monitor 2 --brightness 70 --contrast 50\r\n"
           L"  MonitorControl.exe --monitor \"DELL U2414H\" --brightness +10\r\n";
}

std::wstring JoinArguments(const std::vector<std::wstring>& args)
{
    std::wstring joined;
    for (const std::wstring& arg : args)
    {
        joined += arg;
        joined += L'\0';
    }
    return joined;
}

std::vector<std::wstring> SplitArguments(std::wstring_view joined)
{
    std::vector<std::wstring> args;
    size_t start = 0;
    for (size_t i = 0; i < joined.size(); ++i)
    {
        if (joined[i] == L'\0')
        {
            args.emplace_back(joined.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < joined.size())
        args.emplace_back(joined.substr(start));
    return args;
}
