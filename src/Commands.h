#pragma once

#include <string>
#include <vector>

#include "CommandLine.h"
#include "Ddc.h"

struct CommandResult
{
    int exitCode = 0;      // 0 = ok, 1 = failed, 2 = bad arguments
    std::wstring output;   // lines for the console
};

// Runs a List or Apply command against open monitors. Blocks on DDC I/O.
CommandResult RunCommand(std::vector<DdcMonitor>& monitors, const Command& command);
