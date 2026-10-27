#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Command-line parsing and the pure parts of running a command. Running it against the monitors is
// in Commands.h.

struct Change
{
    uint8_t code = 0;        // VCP feature code
    int value = 0;           // percent, or a signed step when relative
    bool relative = false;   // "+10" / "-10"
};

struct Command
{
    enum class Action
    {
        Tray,    // no arguments: run in the tray
        Apply,
        List,
        Help,
    };

    Action action = Action::Tray;
    std::wstring monitor;   // empty = all monitors; a 1-based number, or a monitor name
    std::vector<Change> changes;
};

struct ParseResult
{
    Command command;
    std::wstring error;   // empty = ok
};

// args excludes the program name.
ParseResult ParseCommandLine(const std::vector<std::wstring>& args);

// Indices of the monitors a --monitor value selects: all when empty, a 1-based number, or every
// monitor with that name (ignoring case). Empty when nothing matches.
std::vector<size_t> SelectMonitors(const std::wstring& selector, const std::vector<std::wstring>& names);

int ResolvePercent(int current, const Change& change);

std::wstring UsageText();

// Arguments packed for WM_COPYDATA: each one terminated by '\0'.
std::wstring JoinArguments(const std::vector<std::wstring>& args);
std::vector<std::wstring> SplitArguments(std::wstring_view joined);
