#pragma once

// Keep the proven clipboard storage/capture engine intact, but place it in a
// private namespace so v0.9.9 can provide a new responsive presentation layer.
#define ClipboardHistory ClipboardHistoryLegacy
#include "ClipboardHistoryImpl.h"
#undef ClipboardHistory

// Win32 RECT/MONITORINFO coordinates are LONG while the responsive layout uses
// int constants. MSVC intentionally does not choose between std::max<int> and
// std::max<LONG>, so provide the two exact mixed overloads used by this UI.
namespace std
{
    inline int max(int a, LONG b) noexcept { return a > b ? a : static_cast<int>(b); }
    inline int min(int a, LONG b) noexcept { return a < b ? a : static_cast<int>(b); }
}

namespace ClipboardHistory
{
    // Keep the normal six-argument draw helper visible inside the new namespace.
    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
        HFONT font, UINT flags)
    {
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }

    // The item-number call site uses the current owner-draw clip as its row
    // rectangle. This small overload supplies the missing rectangle there.
    inline void drawText(HDC dc, const std::wstring& text, COLORREF color, HFONT font, UINT flags)
    {
        RECT clip{};
        GetClipBox(dc, &clip);
        RECT rc{ clip.right - 51, clip.bottom - 29, clip.right - 13, clip.bottom - 3 };
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }
}

// The clipboard window is borderless visually, but still keeps the resize behavior
// supplied by WS_THICKFRAME. Windows normally reserves a non-client resize frame,
// which showed up as the extra white strip around the v0.9.9 UI (especially at the
// top). Intercept only the clipboard shell creation and replace that non-client area
// with our own edge hit testing. The same shim also gives the clipboard a more useful
// compact initial size instead of opening near 1000 x 760 every time.
namespace ClipboardHistoryWindowShim
{
    inline WNDPROC originalClipboardProc{ nullptr };
    inline LRESULT CALLBACK clipboardFrameProc(HWND, UINT, WPARAM, LPARAM);

    inline HWND createWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName,
        DWORD style, int x, int y, int width, int height,
        HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
    {
        const bool isClipboardShell = className && !IS_INTRESOURCE(className) &&
            wcscmp(className, L"StarCapClipboardHistoryV099") == 0;

        if (isClipboardShell) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int workW = std::max(1, (int)(work.right - work.left));
            const int workH = std::max(1, (int)(work.bottom - work.top));

            // Around 820 x 620 on a normal desktop, and proportionally smaller on
            // compact displays. The existing minimum-size rule remains authoritative.
            width = std::min(820, std::max(700, workW * 52 / 100));
            height = std::min(620, std::max(500, workH * 68 / 100));
            x = work.left + (workW - width) / 2;
            y = work.top + (workH - height) / 2;
        }

        HWND created = ::CreateWindowExW(exStyle, className, windowName, style,
            x, y, width, height, parent, menu, instance, param);

        if (isClipboardShell && created) {
            originalClipboardProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(created, GWLP_WNDPROC, (LONG_PTR)clipboardFrameProc));

            // Recalculate the frame immediately so the client canvas reaches all
            // four outer edges on the first visible frame.
            SetWindowPos(created, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        return created;
    }
}

// Route window creation inside the v0.9.9 presentation layer through the shim.
// Child LISTBOX/EDIT/BUTTON controls are passed through unchanged.
#define CreateWindowExW ClipboardHistoryWindowShim::createWindowExW
#include "ClipboardHistoryV099Utools.h"
#undef CreateWindowExW

namespace ClipboardHistoryWindowShim
{
    inline LRESULT CALLBACK clipboardFrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCALCSIZE) {
            // Make the full window rectangle client area. This removes the default
            // white resize-frame inset that was visible around the custom UI.
            return 0;
        }

        if (msg == WM_NCHITTEST && !IsZoomed(hwnd)) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT wr{};
            GetWindowRect(hwnd, &wr);

            const UINT dpi = GetDpiForWindow(hwnd);
            const int edge = std::max(5, MulDiv(6, (int)dpi, 96));
            const bool left = p.x >= wr.left && p.x < wr.left + edge;
            const bool right = p.x < wr.right && p.x >= wr.right - edge;
            const bool top = p.y >= wr.top && p.y < wr.top + edge;
            const bool bottom = p.y < wr.bottom && p.y >= wr.bottom - edge;

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        if (originalClipboardProc)
            return CallWindowProcW(originalClipboardProc, hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
