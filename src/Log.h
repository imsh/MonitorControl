#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// %LOCALAPPDATA%\MonitorControl (created on demand).
std::filesystem::path DataDirectory();
std::filesystem::path LogFilePath();

// Appends a timestamped line to the log file. Thread-safe.
void Log(std::wstring_view message);

std::string ToUtf8(std::wstring_view text);
