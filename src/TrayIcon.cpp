#include "TrayIcon.h"

#include <cwchar>

TrayIcon::TrayIcon(HWND hwnd, UINT id, UINT callbackMessage, HICON icon)
    : hwnd_(hwnd)
    , id_(id)
    , callbackMessage_(callbackMessage)
    , icon_(icon)
{
}

NOTIFYICONDATAW TrayIcon::MakeData() const
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = id_;
    return data;
}

bool TrayIcon::Add()
{
    NOTIFYICONDATAW data = MakeData();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = callbackMessage_;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &data) && !Shell_NotifyIconW(NIM_MODIFY, &data))
        return false;

    data.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
}

void TrayIcon::Remove()
{
    NOTIFYICONDATAW data = MakeData();
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void TrayIcon::SetTooltip(std::wstring_view tooltip)
{
    tooltip_ = tooltip;
    NOTIFYICONDATAW data = MakeData();
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayIcon::ShowBalloon(std::wstring_view title, std::wstring_view text)
{
    const std::wstring titleString(title);
    const std::wstring textString(text);
    NOTIFYICONDATAW data = MakeData();
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(data.szInfoTitle, titleString.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, textString.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
