#include "Flyout.h"

#include <dwmapi.h>

#include <cwchar>
#include <numeric>

namespace
{
constexpr wchar_t kWindowClass[] = L"MonitorControl.Flyout";

// Layout, in device-independent pixels.
constexpr int kWidth = 320;
constexpr int kPadding = 16;
constexpr int kHeadingHeight = 24;
constexpr int kGroupGap = 10;
constexpr int kRowHeight = 32;
constexpr int kLabelWidth = 76;
constexpr int kValueWidth = 44;
constexpr int kScreenMargin = 12;

// A click on the tray icon while the flyout is open first deactivates (hides) it, then arrives as a
// select: within this window it must not reopen it.
constexpr ULONGLONG kReopenGuardMs = 300;
// Don't let a read-back overwrite a slider the user has just moved (the write may still be queued).
constexpr ULONGLONG kUserChangeHoldMs = 1500;

constexpr Flyout::Colors kLight{ RGB(243, 243, 243), RGB(26, 26, 26), RGB(96, 96, 96) };
constexpr Flyout::Colors kDark{ RGB(44, 44, 44), RGB(255, 255, 255), RGB(200, 200, 200) };

bool AppsUseDarkTheme()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}
} // namespace

Flyout::Flyout(HINSTANCE instance, DdcWorker& worker)
    : instance_(instance)
    , worker_(worker)
{
}

Flyout::~Flyout()
{
    if (hwnd_)
        DestroyWindow(hwnd_);
    DeleteObject(font_);
    DeleteObject(headingFont_);
    DeleteObject(backgroundBrush_);
}

bool Flyout::Create()
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DROPSHADOW;
    windowClass.lpfnWndProc = WndProcThunk;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass))
        return false;

    if (!CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWindowClass, L"Monitor Control",
                         WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0, nullptr, nullptr, instance_, this))
        return false;

    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    // Keyboard focus rectangles on the sliders look out of place in a flyout.
    SendMessageW(hwnd_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

    dpi_ = GetDpiForWindow(hwnd_);
    UpdateTheme();
    UpdateFonts();
    Rebuild();
    return true;
}

void Flyout::Toggle(POINT anchor)
{
    if (IsVisible())
        Hide();
    else if (GetTickCount64() - hiddenAt_ > kReopenGuardMs)
        Show(anchor);
}

void Flyout::Show(POINT anchor)
{
    anchor_ = anchor;
    Place(anchor);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    for (const Row& row : rows_)
    {
        if (row.trackbar)
        {
            SetFocus(row.trackbar);
            break;
        }
    }
    // Pick up changes made with the monitor's own buttons.
    worker_.RequestRead();
}

void Flyout::Hide()
{
    if (!IsVisible())
        return;
    ShowWindow(hwnd_, SW_HIDE);
    hiddenAt_ = GetTickCount64();
}

bool Flyout::IsVisible() const
{
    return IsWindowVisible(hwnd_) != FALSE;
}

// Sizes the window for the DPI of the monitor holding the anchor and puts it in that monitor's
// corner next to the taskbar.
void Flyout::Place(POINT anchor)
{
    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    // Moving onto the monitor first makes GetDpiForWindow report that monitor's DPI.
    SetWindowPos(hwnd_, nullptr, info.rcWork.left, info.rcWork.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi != dpi_)
    {
        dpi_ = dpi;
        UpdateFonts();
    }
    Layout();

    RECT window{};
    GetWindowRect(hwnd_, &window);
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int margin = Scale(kScreenMargin);
    int x = info.rcWork.right - width - margin;
    int y = info.rcWork.bottom - height - margin;
    if (info.rcWork.left > info.rcMonitor.left)   // taskbar on the left
        x = info.rcWork.left + margin;
    if (info.rcWork.top > info.rcMonitor.top)     // taskbar at the top
        y = info.rcWork.top + margin;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void Flyout::SetSnapshot(const DdcSnapshot& snapshot)
{
    const bool sameMonitors = snapshot.generation == snapshot_.generation &&
                              snapshot.monitors.size() == snapshot_.monitors.size();
    snapshot_ = snapshot;
    if (!sameMonitors)
    {
        Rebuild();
        if (IsVisible())
            Place(anchor_);
        return;
    }

    if (GetTickCount64() - lastUserChange_ < kUserChangeHoldMs)
        return;
    for (const Row& row : rows_)
    {
        if (row.trackbar && GetCapture() != row.trackbar)
            SetRowValue(row, RowPercent(row));
    }
}

void Flyout::Rebuild()
{
    HWND focused = GetFocus();
    for (const Row& row : rows_)
    {
        for (HWND child : { row.label, row.trackbar, row.value })
        {
            if (child)
                DestroyWindow(child);
        }
    }
    rows_.clear();

    if (snapshot_.generation == 0)
    {
        AddHeading(L"Looking for monitors\x2026");
    }
    else if (snapshot_.monitors.empty())
    {
        AddHeading(L"No DDC/CI monitors found");
    }
    else
    {
        if (snapshot_.monitors.size() > 1)
        {
            AddHeading(L"All monitors");
            AddSlider(L"Brightness", 0, kVcpBrightness, true, MasterPercent());
        }
        for (size_t i = 0; i < snapshot_.monitors.size(); ++i)
        {
            const MonitorState& monitor = snapshot_.monitors[i];
            AddHeading(monitor.name);
            AddSlider(L"Brightness", i, kVcpBrightness, false, monitor.brightness);
            if (monitor.hasContrast)
                AddSlider(L"Contrast", i, kVcpContrast, false, monitor.contrast);
        }
    }
    Layout();

    // The focused slider may just have been destroyed.
    if (IsVisible() && focused && !IsWindow(focused))
    {
        for (const Row& row : rows_)
        {
            if (row.trackbar)
            {
                SetFocus(row.trackbar);
                break;
            }
        }
    }
}

void Flyout::AddHeading(const std::wstring& text)
{
    Row row;
    row.label = CreateWindowExW(0, WC_STATICW, text.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE |
                                SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    rows_.push_back(row);
}

void Flyout::AddSlider(const wchar_t* label, size_t monitor, BYTE code, bool master, int percent)
{
    Row row{ .master = master, .monitor = monitor, .code = code };
    row.label = CreateWindowExW(0, WC_STATICW, label, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                                0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    row.trackbar = CreateWindowExW(0, TRACKBAR_CLASSW, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                   0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    row.value = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX,
                                0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(row.trackbar, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(row.trackbar, TBM_SETLINESIZE, 0, 1);
    SendMessageW(row.trackbar, TBM_SETPAGESIZE, 0, 10);
    SetRowValue(row, percent);
    rows_.push_back(row);
}

void Flyout::SetRowValue(const Row& row, int percent) const
{
    SendMessageW(row.trackbar, TBM_SETPOS, TRUE, percent);
    wchar_t text[16];
    swprintf_s(text, L"%d%%", percent);
    SetWindowTextW(row.value, text);
}

// Positions every child for the current DPI and sizes the window to fit them.
void Flyout::Layout()
{
    const int width = Scale(kWidth);
    const int padding = Scale(kPadding);
    const int rowHeight = Scale(kRowHeight);
    const int labelWidth = Scale(kLabelWidth);
    const int valueWidth = Scale(kValueWidth);
    const int trackbarWidth = width - 2 * padding - labelWidth - valueWidth;

    int y = padding - Scale(4);
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        const Row& row = rows_[i];
        if (!row.trackbar)
        {
            if (i > 0)
                y += Scale(kGroupGap);
            SendMessageW(row.label, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), FALSE);
            MoveWindow(row.label, padding, y, width - 2 * padding, Scale(kHeadingHeight), TRUE);
            y += Scale(kHeadingHeight);
            continue;
        }
        SendMessageW(row.label, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        SendMessageW(row.value, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        MoveWindow(row.label, padding, y, labelWidth, rowHeight, TRUE);
        MoveWindow(row.trackbar, padding + labelWidth, y, trackbarWidth, rowHeight, TRUE);
        MoveWindow(row.value, padding + labelWidth + trackbarWidth, y, valueWidth, rowHeight, TRUE);
        y += rowHeight;
    }
    y += padding - Scale(4);

    RECT rect{ 0, 0, width, y };
    AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                             static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void Flyout::OnScroll(HWND trackbar)
{
    for (const Row& row : rows_)
    {
        if (row.trackbar != trackbar)
            continue;

        const int percent = static_cast<int>(SendMessageW(trackbar, TBM_GETPOS, 0, 0));
        lastUserChange_ = GetTickCount64();
        SetRowValue(row, percent);
        if (row.master)
        {
            for (size_t i = 0; i < snapshot_.monitors.size(); ++i)
            {
                snapshot_.monitors[i].brightness = percent;
                worker_.Set(snapshot_.generation, i, kVcpBrightness, percent);
            }
        }
        else
        {
            MonitorState& monitor = snapshot_.monitors[row.monitor];
            (row.code == kVcpContrast ? monitor.contrast : monitor.brightness) = percent;
            worker_.Set(snapshot_.generation, row.monitor, row.code, percent);
        }

        // Keep the other sliders in step: the master shows the average, the monitors follow the master.
        for (const Row& other : rows_)
        {
            if (other.trackbar && other.trackbar != trackbar && other.code == kVcpBrightness)
                SetRowValue(other, RowPercent(other));
        }
        return;
    }
}

int Flyout::MasterPercent() const
{
    if (snapshot_.monitors.empty())
        return 0;
    const int sum = std::accumulate(snapshot_.monitors.begin(), snapshot_.monitors.end(), 0,
                                    [](int total, const MonitorState& monitor) { return total + monitor.brightness; });
    return (sum + static_cast<int>(snapshot_.monitors.size()) / 2) / static_cast<int>(snapshot_.monitors.size());
}

int Flyout::RowPercent(const Row& row) const
{
    if (row.master)
        return MasterPercent();
    const MonitorState& monitor = snapshot_.monitors[row.monitor];
    return row.code == kVcpContrast ? monitor.contrast : monitor.brightness;
}

void Flyout::UpdateFonts()
{
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_))
        return;

    DeleteObject(font_);
    DeleteObject(headingFont_);
    font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW heading = metrics.lfMessageFont;
    heading.lfWeight = FW_SEMIBOLD;
    headingFont_ = CreateFontIndirectW(&heading);
}

void Flyout::UpdateTheme()
{
    dark_ = AppsUseDarkTheme();
    DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(Palette().background);
    const BOOL dark = dark_;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

int Flyout::Scale(int dip) const
{
    return MulDiv(dip, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
}

const Flyout::Colors& Flyout::Palette() const
{
    return dark_ ? kDark : kLight;
}

LRESULT CALLBACK Flyout::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* flyout = static_cast<Flyout*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        flyout->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(flyout));
    }
    if (auto* flyout = reinterpret_cast<Flyout*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return flyout->WndProc(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Flyout::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            Hide();
        return 0;

    case WM_HSCROLL:
        if (lParam)
            OnScroll(reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_COMMAND:
        // IsDialogMessage (in the message loop) turns Escape into IDCANCEL.
        if (LOWORD(wParam) == IDCANCEL)
            Hide();
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        const Colors& colors = Palette();
        const auto control = reinterpret_cast<HWND>(lParam);
        bool dim = false;
        for (const Row& row : rows_)
            dim = dim || row.value == control || (row.label == control && rows_.size() == 1);
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, dim ? colors.dimText : colors.text);
        SetBkColor(dc, colors.background);
        return reinterpret_cast<LRESULT>(backgroundBrush_);
    }

    case WM_ERASEBKGND:
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, backgroundBrush_);
        return 1;
    }

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
            UpdateTheme();
        return 0;

    case WM_DPICHANGED:
        // Place() lays the window out for the new DPI itself; ignore the suggested rectangle.
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
