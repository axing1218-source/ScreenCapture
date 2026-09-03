#pragma once

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

// StarCap v0.9.9 clipboard presentation layer - rebuilt to follow the
// lightweight uTools list style while preserving StarCap's own image preview
// layout and record count display.

namespace ClipboardHistory
{
    using namespace ClipboardHistoryLegacy;

    inline bool v099UiClassRegistered{ false };
    inline HBRUSH v099ListBrush{ nullptr };
    inline HBRUSH v099SearchBrush{ nullptr };
    inline HFONT v099TitleFont{ nullptr };
    inline HFONT v099IconFont{ nullptr };
    inline HFONT v099TabIconFont{ nullptr };
    inline int v099LastWidth{ 0 };
    inline bool v099ScrollDragging{ false };
    inline int v099ScrollDragOffset{ 0 };

    static constexpr int V099_TOP_H = 60;
    static constexpr int V099_TABS_H = 48;
    static constexpr int V099_HEADER_H = V099_TOP_H + V099_TABS_H;

    inline LRESULT CALLBACK historyProc(HWND, UINT, WPARAM, LPARAM);

    inline bool v099DarkMode()
    {
        auto* setting = Setting::get();
        return setting && setting->getToolFlag(L"app", L"darkMode", false);
    }

    inline COLORREF v099Canvas()      { return v099DarkMode() ? RGB(30, 31, 34) : RGB(254, 254, 254); }
    inline COLORREF v099Card()        { return v099DarkMode() ? RGB(37, 38, 42) : RGB(255, 255, 255); }
    inline COLORREF v099Surface()     { return v099DarkMode() ? RGB(43, 44, 48) : RGB(244, 244, 244); }
    inline COLORREF v099Border()      { return v099DarkMode() ? RGB(62, 64, 69) : RGB(229, 231, 235); }
    inline COLORREF v099Text()        { return v099DarkMode() ? RGB(232, 234, 237) : RGB(32, 33, 36); }
    inline COLORREF v099Muted()       { return v099DarkMode() ? RGB(174, 178, 185) : RGB(112, 117, 124); }
    inline COLORREF v099Faint()       { return v099DarkMode() ? RGB(137, 141, 148) : RGB(154, 160, 166); }
    inline COLORREF v099Primary()     { return RGB(88, 112, 255); }
    inline COLORREF v099PrimarySoft() { return v099DarkMode() ? RGB(47, 52, 76) : RGB(240, 243, 255); }
    inline COLORREF v099Hover()       { return v099DarkMode() ? RGB(42, 43, 47) : RGB(249, 250, 252); }
    inline COLORREF v099SelectedBg()  { return v099DarkMode() ? RGB(35, 37, 44) : RGB(255, 255, 255); }

    inline void v099SyncBrushes()
    {
        if (v099ListBrush) DeleteObject(v099ListBrush);
        if (v099SearchBrush) DeleteObject(v099SearchBrush);
        v099ListBrush = CreateSolidBrush(v099Canvas());
        v099SearchBrush = CreateSolidBrush(v099Surface());

        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(v099Surface());
    }

    inline void v099EnsureFonts(HWND hwnd)
    {
        if (uiFont) return;
        UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        auto px = [dpi](int pt) { return -MulDiv(pt, dpi, 72); };

        uiFont = CreateFontW(px(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        smallFont = CreateFontW(px(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        boldFont = CreateFontW(px(10), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        emojiFont = CreateFontW(px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
        v099TitleFont = CreateFontW(px(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        v099IconFont = CreateFontW(px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
        v099TabIconFont = CreateFontW(px(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
    }

    inline void v099ApplyRoundRegion(HWND hwnd)
    {
        ClipboardHistoryWindowShim::applyNativeFrame(hwnd, v099Border());
    }

    inline RECT v099HeaderBadgeRect() { return { 18, 14, 166, 54 }; }

    inline RECT v099CloseRect(const RECT&)
    {
        return { 136, 20, 158, 48 };
    }

    inline RECT v099SearchOuterRect(const RECT& client)
    {
        return { 180, 16, std::max(360, (int)client.right - 20), 52 };
    }

    inline RECT v099TabRect(const RECT& client, int index)
    {
        const int left = 14;
        const int right = std::max(left + 5, (int)client.right - 14);
        const int span = std::max(102, (right - left) / 5);
        const int x = left + span * index;
        return { x, 68, std::min(right, x + span), 104 };
    }

    inline const BYTE v099AppLogoBgr[32 * 32 * 3] = {
        230, 230, 230, 237, 167, 157, 249, 125, 102, 255, 113, 89, 255, 112, 89, 255, 112, 88, 255, 112, 88, 255, 112, 88,
        255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88, 255, 112, 88,