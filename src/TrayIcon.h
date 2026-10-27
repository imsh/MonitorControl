#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <string_view>

// Thin wrapper over Shell_NotifyIconW (NOTIFYICON_VERSION_4).
class TrayIcon
{
public:
    TrayIcon(HWND hwnd, UINT id, UINT callbackMessage, HICON icon);

    bool Add();   // also call again after the TaskbarCreated message
    void Remove();
    void SetTooltip(std::wstring_view tooltip);
    void ShowBalloon(std::wstring_view title, std::wstring_view text);

private:
    [[nodiscard]] NOTIFYICONDATAW MakeData() const;

    HWND hwnd_;
    UINT id_;
    UINT callbackMessage_;
    HICON icon_;
    std::wstring tooltip_;
};
