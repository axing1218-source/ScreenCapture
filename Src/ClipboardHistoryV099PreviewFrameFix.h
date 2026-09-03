#pragma once

// Final frame cleanup for clipboard preview windows.
// Both preview surfaces are custom-painted popup windows. Keeping WS_THICKFRAME
// makes DWM recreate a native non-client frame after a hide/show cycle, which is
// why the first open can look correct while later opens gain a bright outline.
// The file preview also exposed that native frame as a white strip at the top.
// Remove the native frame completely and keep only our custom-painted surface.
namespace ClipboardHistoryV099PreviewFrameFix
{
    inline WNDPROC fullBaseProc{ nullptr };
    inline WNDPROC fileBaseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };

    inline void disableDwmBorder(HWND hwnd)
    {
        if (!hwnd) return;
        if (auto fn = ClipboardHistoryWindowShim::dwmSetWindowAttribute()) {
            constexpr DWORD DWMWA_BORDER_COLOR_ = 34;
            // Documented DWM sentinel: do not draw a system border.
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
        LONG_PTR wanted = (style & ~remove) | WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU;
        if (wanted != style) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_FRAMECHANGED | SWP_NOCOPYBITS);
        }
        SetWindowRgn(hwnd, nullptr, FALSE);
        disableDwmBorder(hwnd);
    }

    inline LRESULT CALLBACK fullProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCALCSIZE) return 0;
        if (msg == WM_NCHITTEST) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            // Keep the same draggable title area but intentionally remove native
            // resize-edge hit tests because the native resize frame is gone.
            if (p.y >= 0 && p.y < 58 && p.x >= 0 && p.x < rc.right - 330)
                return HTCAPTION;
            return HTCLIENT;
        }

        LRESULT result = fullBaseProc
            ? CallWindowProcW(fullBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_SHOWWINDOW || msg == WM_NCACTIVATE || msg == WM_ACTIVATE ||
            msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) {
            stripNativeFrame(hwnd);
        }
        return result;
    }

    inline LRESULT CALLBACK fileProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCALCSIZE) return 0;
        if (msg == WM_NCHITTEST) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            // File preview has buttons across the upper-right. Only the title/file
            // name region on the left acts as the drag handle.
            if (p.y >= 0 && p.y < 90 && p.x >= 0 && p.x < std::min(285L, rc.right))
                return HTCAPTION;
            return HTCLIENT;
        }

        LRESULT result = fileBaseProc
            ? CallWindowProcW(fileBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_SHOWWINDOW || msg == WM_NCACTIVATE || msg == WM_ACTIVATE ||
            msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) {
            stripNativeFrame(hwnd);
        }
        return result;
    }

    inline void installFull(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        stripNativeFrame(hwnd);
        if (GetPropW(hwnd, L"StarCapV099PreviewFrameFix")) return;
        fullBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)fullProc));
        if (fullBaseProc)
            SetPropW(hwnd, L"StarCapV099PreviewFrameFix", (HANDLE)1);
        stripNativeFrame(hwnd);
    }

    inline void installFile(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        stripNativeFrame(hwnd);
        if (GetPropW(hwnd, L"StarCapV099FileFrameFix")) return;
        fileBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)fileProc));
        if (fileBaseProc)
            SetPropW(hwnd, L"StarCapV099FileFrameFix", (HANDLE)1);
        stripNativeFrame(hwnd);
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
