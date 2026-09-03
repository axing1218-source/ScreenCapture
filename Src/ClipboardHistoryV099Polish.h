#pragma once

// Final v0.9.9 interaction/frame polish.  This layer is deliberately installed
// after the uTools-style presentation and the enhanced preview layer so it can
// fix window-manager/focus/repaint behaviour without touching clipboard data.
namespace ClipboardHistoryV099Polish
{
    inline WNDPROC shellBaseProc{ nullptr };
    inline WNDPROC listBaseProc{ nullptr };
    inline WNDPROC searchBaseProc{ nullptr };
    inline WNDPROC previewBaseProc{ nullptr };
    inline WNDPROC previewEditBaseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    inline HWND shellWindow{ nullptr };
    inline HWND previewWindow{ nullptr };
    inline int wheelRemainder{ 0 };

    static constexpr UINT_PTR INSTALL_TIMER = 0xC19A;

    using DwmExtendFrameFn = HRESULT (WINAPI*)(HWND, const void*);

    struct DwmMargins
    {
        int cxLeftWidth;
        int cxRightWidth;
        int cyTopHeight;
        int cyBottomHeight;
    };

    inline DwmExtendFrameFn dwmExtendFrame()
    {
        static DwmExtendFrameFn fn = []() -> DwmExtendFrameFn {
            HMODULE mod = LoadLibraryW(L"dwmapi.dll");
            if (!mod) return nullptr;
            return reinterpret_cast<DwmExtendFrameFn>(
                GetProcAddress(mod, "DwmExtendFrameIntoClientArea"));
        }();
        return fn;
    }

    inline void enableCompositorRoundedFrame(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;

        // DWM's corner preference is ignored for a number of popup/custom-NC
        // windows.  Keep our custom client chrome, but give the compositor the
        // normal caption/thick-frame style bits it uses to classify a window as
        // roundable. WM_NCCALCSIZE in the existing frame proc still removes the
        // visible system caption, so no native title bar is introduced.
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR wanted = style | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
        if (wanted != style) SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);

        SetWindowRgn(hwnd, nullptr, FALSE);
        ClipboardHistoryWindowShim::applyNativeFrame(hwnd, RGB(229, 231, 235));

        if (auto fn = ClipboardHistoryWindowShim::dwmSetWindowAttribute()) {
            constexpr DWORD DWMWA_NCRENDERING_POLICY_ = 2;
            constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
            constexpr int DWMNCRP_ENABLED_ = 2;
            constexpr int DWMWCP_ROUND_ = 2;
            const int nc = DWMNCRP_ENABLED_;
            const int corner = DWMWCP_ROUND_;
            fn(hwnd, DWMWA_NCRENDERING_POLICY_, &nc, sizeof(nc));
            fn(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &corner, sizeof(corner));
        }

        // A one-pixel DWM frame keeps native composition/shadows/corners active
        // even though the app owns the visible title/header area.
        if (auto extend = dwmExtendFrame()) {
            const DwmMargins margins{ 1, 1, 1, 1 };
            extend(hwnd, &margins);
        }

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }

    inline void invalidateListItem(HWND hwnd, int idx)
    {
        if (!hwnd || idx < 0) return;
        RECT r{};
        if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR)
            InvalidateRect(hwnd, &r, FALSE);
    }

    inline LRESULT CALLBACK polishedSearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Paint directly through the native EDIT proc. The older StarCap search
        // subclass also painted its own placeholder, which doubled the cue and
        // made it appear to need two clicks before disappearing.
        if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
            return ClipboardHistoryLegacy::oldSearchProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_LBUTTONDOWN) {
            SetFocus(hwnd);
            return ClipboardHistoryLegacy::oldSearchProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
            LRESULT r = ClipboardHistoryLegacy::oldSearchProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return r;
        }

        return searchBaseProc
            ? CallWindowProcW(searchBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK polishedListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_ERASEBKGND) return 1;

        if (msg == WM_MOUSEMOVE && !ClipboardHistory::v099ScrollDragging) {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int next = HIWORD(hit) ? -1 : LOWORD(hit);
            if (next < 0 || next >= (int)ClipboardHistoryLegacy::visibleItems.size()) next = -1;

            const int old = ClipboardHistoryLegacy::hoverIndex;
            if (next != old) {
                ClipboardHistoryLegacy::hoverIndex = next;
                invalidateListItem(hwnd, old);
                invalidateListItem(hwnd, next);
            }

            // Skip the legacy hover handler, which invalidated the entire list on
            // every row transition. Let the native listbox receive the move only.
            return ClipboardHistoryLegacy::oldListProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldListProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_MOUSELEAVE && !ClipboardHistory::v099ScrollDragging) {
            const int old = ClipboardHistoryLegacy::hoverIndex;
            ClipboardHistoryLegacy::hoverIndex = -1;
            invalidateListItem(hwnd, old);
            return 0;
        }

        if (msg == WM_MOUSEWHEEL && !ClipboardHistoryLegacy::visibleItems.empty()) {
            wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = wheelRemainder / WHEEL_DELTA;
            wheelRemainder %= WHEEL_DELTA;
            if (notches != 0) {
                UINT wheelLines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
                int rows = wheelLines == WHEEL_PAGESCROLL ? 5 : std::clamp((int)wheelLines, 1, 8);
                const int top = std::max(0, (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
                const int maxIdx = std::max(0, (int)ClipboardHistoryLegacy::visibleItems.size() - 1);
                const int target = std::clamp(top - notches * rows, 0, maxIdx);

                // Prevent LB_SETTOPINDEX from exposing an intermediate paint.
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, target, 0);
                SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                RedrawWindow(hwnd, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
            }
            return 0;
        }

        return listBaseProc
            ? CallWindowProcW(listBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void installSearchAndList()
    {
        HWND list = ClipboardHistoryLegacy::listWnd;
        if (list && IsWindow(list) && !GetPropW(list, L"StarCapV099PolishList")) {
            listBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)polishedListProc));
            SetPropW(list, L"StarCapV099PolishList", (HANDLE)1);
        }

        HWND search = ClipboardHistoryLegacy::searchWnd;
        if (search && IsWindow(search) && !GetPropW(search, L"StarCapV099PolishSearch")) {
            searchBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(search, GWLP_WNDPROC, (LONG_PTR)polishedSearchProc));
            SetPropW(search, L"StarCapV099PolishSearch", (HANDLE)1);
            // FALSE means the native cue disappears as soon as the edit gets focus.
            SendMessageW(search, EM_SETCUEBANNER, FALSE, (LPARAM)L"搜索...");
            InvalidateRect(search, nullptr, FALSE);
        }
    }

    inline LRESULT CALLBACK polishedShellProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_SHOWWINDOW || msg == WM_SIZE || msg == WM_ACTIVATE || msg == WM_SETFOCUS)
            enableCompositorRoundedFrame(hwnd);

        if (msg == WM_SHOWWINDOW && wParam) installSearchAndList();

        if (msg == WM_ERASEBKGND) return 1;

        return shellBaseProc
            ? CallWindowProcW(shellBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT callPreviewEditNativeLayer(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // enhancedPreviewEditBaseProc is the pre-enhancement edit subclass. Using
        // it here preserves normal selection/caret/Esc behaviour without the
        // enhancement layer's old whole-parent invalidation on every mouse move.
        if (ClipboardHistoryWindowShim::enhancedPreviewEditBaseProc)
            return CallWindowProcW(ClipboardHistoryWindowShim::enhancedPreviewEditBaseProc,
                hwnd, msg, wParam, lParam);
        return previewEditBaseProc
            ? CallWindowProcW(previewEditBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void invalidatePreviewScrollbar(HWND parent)
    {
        if (!parent) return;
        RECT r = ClipboardHistoryWindowShim::enhancedTextScrollTrack(parent);
        InflateRect(&r, 3, 3);
        InvalidateRect(parent, &r, FALSE);
    }

    inline LRESULT CALLBACK polishedPreviewEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_MOUSEWHEEL) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            const int perStep = lines == WHEEL_PAGESCROLL ? 8 : std::clamp((int)lines, 1, 8);
            if (steps) SendMessageW(hwnd, EM_LINESCROLL, 0, -steps * perStep);
            invalidatePreviewScrollbar(GetParent(hwnd));
            return 0;
        }

        if (msg == WM_VSCROLL) {
            LRESULT r = callPreviewEditNativeLayer(hwnd, msg, wParam, lParam);
            invalidatePreviewScrollbar(GetParent(hwnd));
            return r;
        }

        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
            msg == WM_LBUTTONDBLCLK || msg == WM_CHAR || msg == WM_KEYDOWN ||
            msg == WM_KEYUP || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
            return callPreviewEditNativeLayer(hwnd, msg, wParam, lParam);
        }

        return previewEditBaseProc
            ? CallWindowProcW(previewEditBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void installPreviewEdit()
    {
        HWND edit = ClipboardHistoryWindowShim::previewEdit;
        if (!edit || !IsWindow(edit) || GetPropW(edit, L"StarCapV099PolishPreviewEdit")) return;
        previewEditBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)polishedPreviewEditProc));
        SetPropW(edit, L"StarCapV099PolishPreviewEdit", (HANDLE)1);
    }

    inline void paintPreviewBuffered(HWND hwnd)
    {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd, &ps);
        if (!target) return;

        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = std::max(1, (int)(rc.right - rc.left));
        const int h = std::max(1, (int)(rc.bottom - rc.top));
        HDC mem = CreateCompatibleDC(target);
        HBITMAP bitmap = mem ? CreateCompatibleBitmap(target, w, h) : nullptr;
        HGDIOBJ oldBitmap = (mem && bitmap) ? SelectObject(mem, bitmap) : nullptr;

        if (mem && bitmap) {
            ClipboardHistoryWindowShim::paintEnhancedPreview(hwnd, mem);
            BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        }
        else {
            ClipboardHistoryWindowShim::paintEnhancedPreview(hwnd, target);
        }

        if (oldBitmap) SelectObject(mem, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        installPreviewEdit();
    }

    inline LRESULT CALLBACK polishedPreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_PAINT) {
            paintPreviewBuffered(hwnd);
            return 0;
        }

        if (msg == WM_ERASEBKGND) return 1;

        if (msg == WM_SHOWWINDOW || msg == WM_SIZE || msg == WM_ACTIVATE || msg == WM_SETFOCUS)
            enableCompositorRoundedFrame(hwnd);

        LRESULT result = previewBaseProc
            ? CallWindowProcW(previewBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if ((msg == WM_SHOWWINDOW && wParam) || msg == WM_SIZE)
            installPreviewEdit();
        return result;
    }

    inline void installShell(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) || GetPropW(hwnd, L"StarCapV099PolishShell")) return;
        shellBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)polishedShellProc));
        if (!shellBaseProc) return;
        shellWindow = hwnd;
        SetPropW(hwnd, L"StarCapV099PolishShell", (HANDLE)1);
        enableCompositorRoundedFrame(hwnd);
        installSearchAndList();
    }

    inline void installPreview(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd) || GetPropW(hwnd, L"StarCapV099PolishPreview")) return;
        previewBaseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)polishedPreviewProc));
        if (!previewBaseProc) return;
        previewWindow = hwnd;
        SetPropW(hwnd, L"StarCapV099PolishPreview", (HANDLE)1);
        enableCompositorRoundedFrame(hwnd);
        installPreviewEdit();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline VOID CALLBACK deferredInstallTimer(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        KillTimer(hwnd, id);
        wchar_t className[96]{};
        GetClassNameW(hwnd, className, (int)std::size(className));
        if (wcscmp(className, L"StarCapClipboardHistoryV099") == 0) installShell(hwnd);
        else if (wcscmp(className, L"StarCapClipboardFullView") == 0) installPreview(hwnd);
    }

    inline void CALLBACK onWindowShown(HWINEVENTHOOK, DWORD event, HWND hwnd,
        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_OBJECT_SHOW || idObject != OBJID_WINDOW || !hwnd) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return;

        wchar_t className[96]{};
        GetClassNameW(hwnd, className, (int)std::size(className));
        if (wcscmp(className, L"StarCapClipboardHistoryV099") != 0 &&
            wcscmp(className, L"StarCapClipboardFullView") != 0) return;

        // Defer by one UI tick so the existing enhancement/frame subclasses are
        // definitely installed first, independent of WinEvent callback order.
        SetTimer(hwnd, INSTALL_TIMER, 1, deferredInstallTimer);
    }

    struct HookLifetime
    {
        HookLifetime()
        {
            showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                onWindowShown, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
        }
        ~HookLifetime()
        {
            if (showHook) {
                UnhookWinEvent(showHook);
                showHook = nullptr;
            }
        }
    };

    inline HookLifetime hookLifetime;
}
