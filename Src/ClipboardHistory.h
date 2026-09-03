#pragma once

// Clipboard shell and full-preview window shim. It is declared before the legacy
// implementation so both legacy preview creation and the v0.9.9 shell creation can
// be intercepted without changing the proven clipboard storage engine.
namespace ClipboardHistoryWindowShim
{
    inline WNDPROC originalClipboardProc{ nullptr };
    inline WNDPROC originalPreviewProc{ nullptr };
    inline WNDPROC originalPreviewEditProc{ nullptr };
    inline HWND previewEdit{ nullptr };
    inline HBRUSH previewEditBrush{ nullptr };
    inline bool previewCopyFeedback{ false };
    static constexpr UINT_PTR PREVIEW_COPY_TIMER = 0xC099;

    inline LRESULT CALLBACK clipboardFrameProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK previewFrameProc(HWND, UINT, WPARAM, LPARAM);
    inline LRESULT CALLBACK previewEditProc(HWND, UINT, WPARAM, LPARAM);
    inline void syncPreviewContent(HWND hwnd);

    using DwmSetWindowAttributeFn = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

    inline DwmSetWindowAttributeFn dwmSetWindowAttribute()
    {
        static DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
            HMODULE mod = LoadLibraryW(L"dwmapi.dll");
            if (!mod) return nullptr;
            return reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(mod, "DwmSetWindowAttribute"));
        }();
        return fn;
    }

    inline void applyNativeFrame(HWND hwnd, COLORREF border = RGB(229, 231, 235))
    {
        if (!hwnd) return;

        // Remove the old GDI region. CreateRoundRectRgn is pixel-snapped and was the
        // main source of the jagged outer corners seen in the clipboard window.
        SetWindowRgn(hwnd, nullptr, FALSE);

        if (auto fn = dwmSetWindowAttribute()) {
            constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
            constexpr DWORD DWMWA_BORDER_COLOR_ = 34;
            constexpr int DWMWCP_ROUND_ = 2;
            const int corner = DWMWCP_ROUND_;
            fn(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &corner, sizeof(corner));
            fn(hwnd, DWMWA_BORDER_COLOR_, &border, sizeof(border));
        }
    }

    inline HWND createWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName,
        DWORD style, int x, int y, int width, int height,
        HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
    {
        const bool validClass = className && !IS_INTRESOURCE(className);
        const bool isClipboardShell = validClass && wcscmp(className, L"StarCapClipboardHistoryV099") == 0;
        const bool isFullPreview = validClass && wcscmp(className, L"StarCapClipboardFullView") == 0;

        if (isClipboardShell || isFullPreview) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int workW = (int)(work.right - work.left) > 1 ? (int)(work.right - work.left) : 1;
            const int workH = (int)(work.bottom - work.top) > 1 ? (int)(work.bottom - work.top) : 1;

            if (isClipboardShell) {
                width = std::min(820, std::max(700, workW * 52 / 100));
                height = std::min(620, std::max(500, workH * 68 / 100));
            }
            else {
                // The legacy preview opened close to 1000x760. Keep it intentionally compact.
                width = std::min(760, std::max(620, workW * 46 / 100));
                height = std::min(540, std::max(420, workH * 58 / 100));
                style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU;
            }
            x = work.left + (workW - width) / 2;
            y = work.top + (workH - height) / 2;
        }

        HWND created = ::CreateWindowExW(exStyle, className, windowName, style,
            x, y, width, height, parent, menu, instance, param);

        if (isClipboardShell && created) {
            originalClipboardProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(created, GWLP_WNDPROC, (LONG_PTR)clipboardFrameProc));
            applyNativeFrame(created);
            SetWindowPos(created, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        else if (isFullPreview && created) {
            originalPreviewProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(created, GWLP_WNDPROC, (LONG_PTR)previewFrameProc));
            applyNativeFrame(created);
            SetWindowPos(created, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            syncPreviewContent(created);
        }
        return created;
    }
}

// Intercept legacy preview creation while moving the storage/capture engine into
// its private namespace.
#define CreateWindowExW ClipboardHistoryWindowShim::createWindowExW
#define ClipboardHistory ClipboardHistoryLegacy
#include "ClipboardHistoryImpl.h"
#undef ClipboardHistory
#undef CreateWindowExW

// Win32 RECT/MONITORINFO coordinates are LONG while the responsive layout uses
// int constants. Keep the two exact mixed overloads used by the v0.9.9 UI.
namespace std
{
    inline int max(int a, LONG b) noexcept { return a > b ? a : static_cast<int>(b); }
    inline int min(int a, LONG b) noexcept { return a < b ? a : static_cast<int>(b); }
}

namespace ClipboardHistory
{
    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
        HFONT font, UINT flags)
    {
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }

    inline void drawText(HDC dc, const std::wstring& text, COLORREF color, HFONT font, UINT flags)
    {
        RECT clip{};
        GetClipBox(dc, &clip);
        RECT rc{ clip.right - 51, clip.bottom - 29, clip.right - 13, clip.bottom - 3 };
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }
}

namespace ClipboardHistoryWindowShim
{
    inline RECT previewCopyRect(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        return { rc.right - 146, 12, rc.right - 102, 50 };
    }

    inline RECT previewCloseRect(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        return { rc.right - 94, 12, rc.right - 18, 50 };
    }

    inline void layoutPreviewEdit(HWND hwnd)
    {
        if (!previewEdit || !IsWindow(previewEdit)) return;
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int x = 36;
        const int y = 80;
        const int w = std::max(80, (int)rc.right - 72);
        const int h = std::max(80, (int)rc.bottom - 116);
        MoveWindow(previewEdit, x, y, w, h, TRUE);
    }

    inline void ensurePreviewEdit(HWND hwnd)
    {
        if (previewEdit && IsWindow(previewEdit)) return;

        previewEdit = ::CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!previewEdit) return;

        SendMessageW(previewEdit, WM_SETFONT, (WPARAM)ClipboardHistoryLegacy::uiFont, TRUE);
        SendMessageW(previewEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        originalPreviewEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(previewEdit, GWLP_WNDPROC, (LONG_PTR)previewEditProc));

        if (!previewEditBrush) previewEditBrush = CreateSolidBrush(RGB(244, 244, 244));
        layoutPreviewEdit(hwnd);
    }

    inline void syncPreviewContent(HWND hwnd)
    {
        using namespace ClipboardHistoryLegacy;
        auto* item = findByHash(fullItemHash);
        if (!item) return;

        ensurePreviewEdit(hwnd);
        if (!previewEdit) return;

        if (item->type == ItemType::Image) {
            ShowWindow(previewEdit, SW_HIDE);
            return;
        }

        std::wstring text = item->type == ItemType::Text
            ? textFromData(item->data)
            : filePreview(item->data, 1000);
        SetWindowTextW(previewEdit, text.c_str());
        SendMessageW(previewEdit, EM_SETSEL, 0, 0);
        ShowWindow(previewEdit, SW_SHOW);
        layoutPreviewEdit(hwnd);
    }

    inline void restoreClipboardFocus()
    {
        if (ClipboardHistoryLegacy::historyWnd && IsWindow(ClipboardHistoryLegacy::historyWnd)) {
            applyNativeFrame(ClipboardHistoryLegacy::historyWnd);
            RedrawWindow(ClipboardHistoryLegacy::historyWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
            ShowWindow(ClipboardHistoryLegacy::historyWnd, SW_SHOW);
            SetForegroundWindow(ClipboardHistoryLegacy::historyWnd);
            if (ClipboardHistoryLegacy::listWnd && IsWindow(ClipboardHistoryLegacy::listWnd))
                SetFocus(ClipboardHistoryLegacy::listWnd);
        }
    }

    inline LRESULT CALLBACK previewEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            HWND parent = GetParent(hwnd);
            if (parent) {
                ShowWindow(parent, SW_HIDE);
                restoreClipboardFocus();
            }
            return 0;
        }
        return originalPreviewEditProc
            ? CallWindowProcW(originalPreviewEditProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK previewFrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_NCCALCSIZE:
            return 0;

        case WM_NCACTIVATE:
            applyNativeFrame(hwnd);
            return TRUE;

        case WM_NCHITTEST:
        {
            if (!IsZoomed(hwnd)) {
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT wr{}; GetWindowRect(hwnd, &wr);
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

            POINT client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &client);
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (client.y < 58 && client.x < rc.right - 330) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_SIZE:
            applyNativeFrame(hwnd);
            layoutPreviewEdit(hwnd);
            break;

        case WM_SHOWWINDOW:
            if (wParam) {
                applyNativeFrame(hwnd);
                syncPreviewContent(hwnd);
            }
            break;

        case WM_CTLCOLOREDIT:
            if ((HWND)lParam == previewEdit) {
                HDC dc = (HDC)wParam;
                SetTextColor(dc, RGB(32, 33, 36));
                SetBkColor(dc, RGB(244, 244, 244));
                return (LRESULT)previewEditBrush;
            }
            break;

        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT copy = previewCopyRect(hwnd);
            RECT close = previewCloseRect(hwnd);
            if (PtInRect(&copy, p)) {
                ClipboardHistoryLegacy::restoreByHash(ClipboardHistoryLegacy::fullItemHash, false);
                previewCopyFeedback = true;
                SetTimer(hwnd, PREVIEW_COPY_TIMER, 900, nullptr);
                InvalidateRect(hwnd, &copy, FALSE);
                return 0;
            }
            if (PtInRect(&close, p)) {
                ShowWindow(hwnd, SW_HIDE);
                restoreClipboardFocus();
                return 0;
            }
            break;
        }

        case WM_TIMER:
            if (wParam == PREVIEW_COPY_TIMER) {
                KillTimer(hwnd, PREVIEW_COPY_TIMER);
                previewCopyFeedback = false;
                RECT copy = previewCopyRect(hwnd);
                InvalidateRect(hwnd, &copy, FALSE);
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                restoreClipboardFocus();
                return 0;
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            restoreClipboardFocus();
            return 0;

        case WM_PAINT:
        {
            LRESULT result = originalPreviewProc
                ? CallWindowProcW(originalPreviewProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);

            if (previewCopyFeedback) {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    RECT copy = previewCopyRect(hwnd);
                    ClipboardHistoryLegacy::fillRoundRect(dc, copy, RGB(240, 243, 255), 8);
                    ClipboardHistoryLegacy::drawText(dc, L"已复制", copy, RGB(88, 112, 255),
                        ClipboardHistoryLegacy::smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    ReleaseDC(hwnd, dc);
                }
            }
            return result;
        }

        case WM_DESTROY:
            previewEdit = nullptr;
            originalPreviewEditProc = nullptr;
            break;
        }

        return originalPreviewProc
            ? CallWindowProcW(originalPreviewProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK clipboardFrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCALCSIZE) return 0;

        if (msg == WM_NCACTIVATE) {
            applyNativeFrame(hwnd);
            return TRUE;
        }

        if (msg == WM_SIZE || msg == WM_SHOWWINDOW || msg == WM_ACTIVATE || msg == WM_SETFOCUS)
            applyNativeFrame(hwnd);

        if (msg == WM_NCHITTEST && !IsZoomed(hwnd)) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT wr{}; GetWindowRect(hwnd, &wr);
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

// Route v0.9.9 shell creation through the same frame shim. Child controls are
// passed through unchanged.
#define CreateWindowExW ClipboardHistoryWindowShim::createWindowExW
#include "ClipboardHistoryV099Utools.h"
#undef CreateWindowExW
