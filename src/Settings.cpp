#include "Settings.h"

#include <windows.h>

#include <cwchar>
#include <string>

namespace
{
constexpr wchar_t kSettingsKey[] = L"Software\\MonitorControl";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"MonitorControl";

std::wstring StartupCommand()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size())
        {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return L"\"" + path + L"\"";
}
} // namespace

bool ReadBoolSetting(const wchar_t* name, bool defaultValue)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return defaultValue;
    return value != 0;
}

void WriteBoolSetting(const wchar_t* name, bool value)
{
    const DWORD data = value ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &data, sizeof(data));
}

bool IsStartWithWindowsEnabled()
{
    wchar_t value[1024];
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
        return false;
    return _wcsicmp(value, StartupCommand().c_str()) == 0;
}

void SetStartWithWindows(bool enable)
{
    if (enable)
    {
        const std::wstring command = StartupCommand();
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    }
}
