#pragma once

// Final pointer/capture fix for the v0.9.9 Win32 ListBox shell.
// The native ListBox starts a capture/drag-selection cycle on WM_LBUTTONDOWN.
// Our custom WM_LBUTTONUP handler used to consume the release before the native
// ListBox saw it, leaving capture active. That is what caused the cursor-near-edge
// auto-scroll and visible row jumps. Own ordinary row clicks completely here and
// only delegate to the lower chain for the custom floating scrollbar.
namespace ClipboardHistoryV099PointerFix
{
    inline WNDPROC baseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    inline int pressedIndex{ -1 };
    inline bool rowPress{ false };
    static constexpr UINT_PTR INSTALL_TIMER = 0xC19C;

    inline void invalidateItem(HWND hwnd, int idx)
    {
        if (!hwnd || idx < 0) return;
        RECT r{};
        if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR)
            InvalidateRect(hwnd, &r, FALSE);
    }

    inline void selectWithoutScrolling(HWND hwnd, int idx)
    {
        if (!hwnd || idx < 0) return;
        const int old = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
        const int top = std::max(0, (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));

        // LB_SETCURSEL is allowed to scroll a variable-height ListBox to expose
        // the selected record. Preserve the viewport explicitly so clicking a
        // large image row never shifts the list underneath the pointer.
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
        SendMessageW(hwnd, LB_SETTOPINDEX, top, 0);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);

        if (old != idx) invalidateItem(hwnd, old);
        invalidateItem(hwnd, idx);
    }

    inline bool isFloatingScrollbarHit(HWND hwnd, POINT p)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        return p.x >= rc.right - 12;
    }

    inline LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            // The custom floating thumb intentionally uses capture while dragging.
            // Let the existing v0.9.9 scrollbar code own clicks in that narrow strip.
            if (isFloatingScrollbarHit(hwnd, p)) {
                rowPress = false;
                pressedIndex = -1;
                return baseProc
                    ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            const int idx = HIWORD(hit) ? -1 : LOWORD(hit);
            if (idx < 0 || idx >= (int)ClipboardHistoryLegacy::visibleItems.size()) {
                rowPress = false;
                pressedIndex = -1;
                ReleaseCapture();
                return 0;
            }

            SetFocus(hwnd);
            ReleaseCapture();
            rowPress = true;
            pressedIndex = idx;
            selectWithoutScrolling(hwnd, idx);
            ClipboardHistoryLegacy::multiAnchor = idx;

            // Do not forward ordinary row mouse-down to the native ListBox. Doing
            // so starts LB's built-in drag-selection/autoscroll capture state.
            return 0;
        }

        case WM_MOUSEMOVE:
            if (ClipboardHistory::v099ScrollDragging) {
                return baseProc
                    ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            // No row-hover animation and, crucially, no native drag-selection path.
            return 0;

        case WM_LBUTTONUP:
        {
            if (ClipboardHistory::v099ScrollDragging) {
                rowPress = false;
                pressedIndex = -1;
                LRESULT r = baseProc
                    ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
                ReleaseCapture();
                return r;
            }

            const int idx = pressedIndex;
            rowPress = false;
            pressedIndex = -1;
            ReleaseCapture();
            if (idx < 0 || idx >= (int)ClipboardHistoryLegacy::visibleItems.size()) return 0;

            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            const int upIdx = HIWORD(hit) ? -1 : LOWORD(hit);
            if (upIdx != idx) return 0;

            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift) {
                ClipboardHistoryLegacy::selectRangeTo(idx);
                ReleaseCapture();
                return 0;
            }

            RECT itemRc{};
            if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc) != LB_ERR) {
                RECT expand = ClipboardHistoryLegacy::expandRect(itemRc);
                if (ClipboardHistoryLegacy::pointIn(expand, p)) {
                    ClipboardHistoryLegacy::showFull(idx);
                    return 0;
                }
            }

            if (ClipboardHistoryLegacy::multiMode) {
                ClipboardHistoryLegacy::multiMode = false;
                ClipboardHistoryLegacy::multiHashes.clear();
                if (ClipboardHistoryLegacy::searchWnd)
                    ShowWindow(ClipboardHistoryLegacy::searchWnd, SW_SHOW);
            }
            invalidateItem(hwnd, idx);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            // stableListProc below already implements double-click paste without
            // moving the record to the top. Keep that behavior.
            ReleaseCapture();
            rowPress = false;
            pressedIndex = -1;
            return baseProc
                ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_MOUSEWHEEL:
            // Defensive cleanup for any capture left by an interrupted click.
            rowPress = false;
            pressedIndex = -1;
            ReleaseCapture();
            {
                LRESULT r = baseProc
                    ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
                ReleaseCapture();
                return r;
            }

        case WM_CANCELMODE:
        case WM_KILLFOCUS:
            rowPress = false;
            pressedIndex = -1;
            ReleaseCapture();
            break;

        case WM_CAPTURECHANGED:
            if (!ClipboardHistory::v099ScrollDragging) {
                rowPress = false;
                pressedIndex = -1;
            }
            break;
        }

        return baseProc
            ? CallWindowProcW(baseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void installNow()
    {
        HWND list = ClipboardHistoryLegacy::listWnd;
        if (!list || !IsWindow(list) || GetPropW(list, L"StarCapV099PointerFix")) return;

        baseProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)proc));
        if (!baseProc) return;
        SetPropW(list, L"StarCapV099PointerFix", (HANDLE)1);
        ReleaseCapture();
    }

    inline VOID CALLBACK installTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        if (hwnd && IsWindow(hwnd)) KillTimer(hwnd, id);
        installNow();
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
        if (wcscmp(cls, L"StarCapClipboardHistoryV099") == 0) {
            // Stable installs at 30 ms. Install this last pointer layer later so
            // it is guaranteed to be the outermost ListBox subclass.
            SetTimer(hwnd, INSTALL_TIMER, 90, installTimerProc);
        }
    }

    struct Lifetime
    {
        Lifetime()
        {
            showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                onShow, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
        }
        ~Lifetime()
        {
            if (showHook) UnhookWinEvent(showHook);
        }
    };

    inline Lifetime lifetime;
}
