#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "DdcWorker.h"

// The popup with the sliders, shown next to the tray icon like the volume flyout.
// Hides itself when it loses activation.
class Flyout
{
public:
    struct Colors
    {
        COLORREF background;
        COLORREF text;
        COLORREF dimText;
    };

    Flyout(HINSTANCE instance, DdcWorker& worker);
    ~Flyout();
    Flyout(const Flyout&) = delete;
    Flyout& operator=(const Flyout&) = delete;

    bool Create();
    void Toggle(POINT anchor);
    void Hide();
    [[nodiscard]] bool IsVisible() const;
    [[nodiscard]] HWND Hwnd() const { return hwnd_; }

    void SetSnapshot(const DdcSnapshot& snapshot);

private:
    struct Row
    {
        HWND label = nullptr;
        HWND trackbar = nullptr;   // null for a heading
        HWND value = nullptr;
        bool master = false;       // the "All monitors" brightness slider
        size_t monitor = 0;
        BYTE code = 0;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Show(POINT anchor);
    void Place(POINT anchor);
    void Rebuild();
    void Layout();
    void AddHeading(const std::wstring& text);
    void AddSlider(const wchar_t* label, size_t monitor, BYTE code, bool master, int percent);
    void SetRowValue(const Row& row, int percent) const;
    void OnScroll(HWND trackbar);
    void UpdateFonts();
    void UpdateTheme();

    [[nodiscard]] int MasterPercent() const;
    [[nodiscard]] int RowPercent(const Row& row) const;
    [[nodiscard]] int Scale(int dip) const;
    [[nodiscard]] const Colors& Palette() const;

    HINSTANCE instance_;
    DdcWorker& worker_;
    HWND hwnd_ = nullptr;
    DdcSnapshot snapshot_;
    std::vector<Row> rows_;
    POINT anchor_{};
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    HFONT font_ = nullptr;
    HFONT headingFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    bool dark_ = false;
    ULONGLONG hiddenAt_ = 0;
    ULONGLONG lastUserChange_ = 0;
};
