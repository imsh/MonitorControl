#pragma once

// App settings in HKCU\Software\MonitorControl, plus the HKCU Run entry.

bool ReadBoolSetting(const wchar_t* name, bool defaultValue);
void WriteBoolSetting(const wchar_t* name, bool value);

bool IsStartWithWindowsEnabled();
void SetStartWithWindows(bool enable);
