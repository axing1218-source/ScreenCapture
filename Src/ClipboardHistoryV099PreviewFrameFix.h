#pragma once

// Unified floating-frame treatment for clipboard preview windows.
// The OS/DWM is not allowed to paint the outline: hide/show cycles can otherwise
// recreate a bright native non-client border. StarCap owns the 1px outline entirely.
namespace ClipboardHistoryV099PreviewFrameFix
{
    inline WNDPROC fullBaseProc{ nullptr };
    inline WNDPROC fileBaseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };

    inline COLORREF floatingBorderColor()
    {
        return ClipboardHistory::v099DarkMode()
            ? RGB(68, 76, 96)       // #444C60
            : RGB(205, 210, 220);   // #CDD2DC
    }

    inline void disableDwmBorder(HWND hwnd)
    {
        if (!hwnd) return;
        if (auto fn = ClipboardHistoryWindowShim::dwmSetWindowAttribute()) {
            constexpr DWORD DWMWA_BORDER_COLOR_ = 34;
            constexpr COLORREF DWMWA_COLOR_NONE_ = 0xFFFFFFFEu;
            const COLORREF none = DWMWA_COLOR_NONE_;
            fn(hwnd, DWMWA_BORDER_COLOR_, &none, sizeof(none));
        }
    }

    inline void stripNativeFrame(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR remove = WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_CAPTION;
        const LONG_PTR wanted = (style & ~remove) | WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU;
        if (wanted != style) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_FRAMECHANGED | SWP_NOCOPYBITS);
        }
        SetWindowRgn(hwnd, nullptr, FALSE);
        disableDwmBorder(hwnd);
    }

    inline void paintFloatingBorder(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;
        HDC dc = GetDC(hwnd);
        if (!dc) return;

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = std::max(1L, rc.right - rc.left);
        const int h = std::max(1L, rc.bottom - rc.top);
        const COLORREF color = floatingBorderColor();

        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = pen ? SelectObject(dc, pen) : nullptr;
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, w, h);
        if (oldBrush) SelectObject(dc, oldBrush);
        if (oldPen) SelectObject(dc, oldPen);
        if (pen) DeleteObject(pen);
        ReleaseDC(hwnd, dc);
    }

    inline void refreshFullPreview(HWND hwnd)
    {
        stripNativeFrame(hwnd);
        ClipboardHistoryWindowShim::layoutPreviewEdit(hwnd);
        if (ClipboardHistoryWindowShim::previewEdit &&
            IsWindow(ClipboardHistoryWindowShim::previewEdit)) {
            InvalidateRect(ClipboardHistoryWindowShim::previewEdit, nullptr, TRUE);
        }
        RedrawWindow(hwnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        paintFloatingBorder(hwnd);
    }

    inline bool isFrameResetMessage(UINT msg)
    {
        return msg == WM_STYLECHANGED || msg == WM_THEMECHANGED ||
            msg == WM_DWMCOMPOSITIONCHANGED || msg == WM_DISPLAYCHANGE ||
            msg == WM_SETTINGCHANGE;
    }

    inline LRESULT CALLBACK fullProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_NCCALCSIZE:
            return 0;

        case WM_NCPAINT:
            // Never let DefWindowProc / the legacy preview proc paint a native frame.
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return 0;

        case WM_NCACTIVATE:
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return TRUE;

        case WM_SHOWWINDOW:
            if (wParam) {
                stripNativeFrame(hwnd);
                ClipboardHistoryWindowShim::syncPreviewContent(hwnd);
                refreshFullPreview(hwnd);
            } else {
                stripNativeFrame(hwnd);
            }
            return 0;

        case WM_SIZE:
            refreshFullPreview(hwnd);
            return 0;

        case WM_NCHITTEST:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (p.y >= 0 && p.y < 58 && p.x >= 0 && p.x < rc.right - 330)
                return HTCAPTION;
            return HTCLIENT;
        }

        case WM_PAINT:
        {
            LRESULT result = fullBaseProc
                ? CallWindowProcW(fullBaseProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return result;
        }
        }

        LRESULT result = fullBaseProc
            ? CallWindowProcW(fullBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_ACTIVATE || msg == WM_WINDOWPOSCHANGED || msg == WM_SETFOCUS ||
            isFrameResetMessage(msg)) {
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
        }
        return result;
    }

    inline LRESULT CALLBACK fileProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCALCSIZE) return 0;
        if (msg == WM_NCPAINT) {
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return 0;
        }
        if (msg == WM_NCACTIVATE) {
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return TRUE;
        }
        if (msg == WM_NCHITTEST) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (p.y >= 0 && p.y < 90 && p.x >= 0 && p.x < std::min(285L, rc.right))
                return HTCAPTION;
            return HTCLIENT;
        }

        if (msg == WM_PAINT) {
            LRESULT result = fileBaseProc
                ? CallWindowProcW(fileBaseProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
            return result;
        }

        LRESULT result = fileBaseProc
            ? CallWindowProcW(fileBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_SHOWWINDOW || msg == WM_ACTIVATE || msg == WM_SIZE ||
            msg == WM_WINDOWPOSCHANGED || msg == WM_SETFOCUS || isFrameResetMessage(msg)) {
            stripNativeFrame(hwnd);
            paintFloatingBorder(hwnd);
        }
        return result;
    }

    inline void installFull(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        stripNativeFrame(hwnd);
        if (GetPropW(hwnd, L"StarCapV099PreviewFrameFix")) {
            paintFloatingBorder(hwnd);
            return;
        }
        fullBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)fullProc));
        if (fullBaseProc)
            SetPropW(hwnd, L"StarCapV099PreviewFrameFix", (HANDLE)1);
        stripNativeFrame(hwnd);
        paintFloatingBorder(hwnd);
    }

    inline void installFile(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        stripNativeFrame(hwnd);
        if (GetPropW(hwnd, L"StarCapV099FileFrameFix")) {
            paintFloatingBorder(hwnd);
            return;
        }
        fileBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)fileProc));
        if (fileBaseProc)
            SetPropW(hwnd, L"StarCapV099FileFrameFix", (HANDLE)1);
        stripNativeFrame(hwnd);
        paintFloatingBorder(hwnd);
    }

    inline void CALLBACK onShow(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;

        wchar_t cls[96]{};
        GetClassNameW(hwnd, cls, (int)std::size(cls));
        if (wcscmp(cls, L"StarCapClipboardFullView") == 0)
            installFull(hwnd);
        else if (wcscmp(cls, L"StarCapClipboardFilePreviewV099") == 0)
            installFile(hwnd);
    }

    struct Lifetime
    {
        Lifetime()
        {
            showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                onShow, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
            if (ClipboardHistoryLegacy::fullWnd && IsWindow(ClipboardHistoryLegacy::fullWnd))
                installFull(ClipboardHistoryLegacy::fullWnd);
            if (ClipboardHistoryV099FilePreview::window &&
                IsWindow(ClipboardHistoryV099FilePreview::window))
                installFile(ClipboardHistoryV099FilePreview::window);
        }
        ~Lifetime()
        {
            if (showHook) UnhookWinEvent(showHook);
        }
    };

    inline Lifetime lifetime;
}
