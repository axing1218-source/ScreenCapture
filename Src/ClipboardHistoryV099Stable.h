#pragma once

// v0.9.9 stability layer.
// Keep the clipboard on StarCap's existing square-corner Win32 window model and
// remove interaction repaint/reorder effects that made the shell visibly jump.
namespace ClipboardHistoryV099Stable
{
    inline WNDPROC listBaseProc{ nullptr };
    inline WNDPROC searchBaseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    static constexpr UINT_PTR INSTALL_TIMER = 0xC19B;

    inline void invalidateItem(HWND hwnd, int idx)
    {
        if (!hwnd || idx < 0) return;
        RECT r{};
        if (SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR)
            InvalidateRect(hwnd, &r, FALSE);
    }

    inline bool useItemWithoutReorder(int idx, bool paste)
    {
        auto* item = ClipboardHistoryLegacy::itemAtListIndex(idx);
        if (!item) return false;
        if (!ClipboardHistoryLegacy::restoreItem(*item)) return false;
        if (paste) ClipboardHistoryLegacy::pasteToPrevious();
        return true;
    }

    inline LRESULT CALLBACK stableSearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Use exactly one placeholder implementation. The legacy search subclass
        // drew its own hint even while focused; the native cue was inconsistent
        // after the later subclass chain. Paint one hint ourselves only when the
        // edit is empty AND unfocused, so the first click always clears it.
        if (msg == WM_PAINT) {
            LRESULT result = ClipboardHistoryLegacy::oldSearchProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);

            if (GetWindowTextLengthW(hwnd) == 0 && GetFocus() != hwnd) {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    RECT r{};
                    GetClientRect(hwnd, &r);
                    r.left += 7;
                    r.right -= 7;
                    ClipboardHistoryLegacy::drawText(dc, L"搜索...", r,
                        ClipboardHistory::v099Muted(), ClipboardHistoryLegacy::uiFont,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    ReleaseDC(hwnd, dc);
                }
            }
            return result;
        }

        if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
            LRESULT result = searchBaseProc
                ? CallWindowProcW(searchBaseProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
        }

        return searchBaseProc
            ? CallWindowProcW(searchBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK stableListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Remove the row-hover repaint effect completely. It added no function and
        // caused repeated redraws while the pointer crossed records.
        if (msg == WM_MOUSEMOVE && !ClipboardHistory::v099ScrollDragging) {
            if (ClipboardHistoryLegacy::hoverIndex != -1) {
                const int old = ClipboardHistoryLegacy::hoverIndex;
                ClipboardHistoryLegacy::hoverIndex = -1;
                invalidateItem(hwnd, old);
            }
            return ClipboardHistoryLegacy::oldListProc
                ? CallWindowProcW(ClipboardHistoryLegacy::oldListProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_MOUSELEAVE && !ClipboardHistory::v099ScrollDragging) {
            if (ClipboardHistoryLegacy::hoverIndex != -1) {
                const int old = ClipboardHistoryLegacy::hoverIndex;
                ClipboardHistoryLegacy::hoverIndex = -1;
                invalidateItem(hwnd, old);
            }
            return 0;
        }

        // Reimplement the ordinary single-click path locally. The legacy handler
        // invalidated historyWnd after every selection, repainting the whole header
        // and all five tabs even though only the old/new rows changed.
        if (msg == WM_LBUTTONUP && !ClipboardHistory::v099ScrollDragging) {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (HIWORD(hit) || idx < 0 || idx >= (int)ClipboardHistoryLegacy::visibleItems.size())
                return 0;

            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift) {
                ClipboardHistoryLegacy::selectRangeTo(idx);
                return 0;
            }

            RECT itemRc{};
            SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT expand = ClipboardHistoryLegacy::expandRect(itemRc);

            const int old = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            ClipboardHistoryLegacy::multiAnchor = idx;

            if (ClipboardHistoryLegacy::pointIn(expand, p)) {
                ClipboardHistoryLegacy::showFull(idx);
                return 0;
            }

            if (ClipboardHistoryLegacy::multiMode) {
                ClipboardHistoryLegacy::multiMode = false;
                ClipboardHistoryLegacy::multiHashes.clear();
                if (ClipboardHistoryLegacy::searchWnd)
                    ShowWindow(ClipboardHistoryLegacy::searchWnd, SW_SHOW);
            }

            if (old != idx) invalidateItem(hwnd, old);
            invalidateItem(hwnd, idx);
            return 0;
        }

        // Reusing a clipboard record should not visibly jump it to the top. Keep
        // its original position/timestamp when copying or pasting from history.
        if (msg == WM_LBUTTONDBLCLK) {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)ClipboardHistoryLegacy::visibleItems.size()) {
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                useItemWithoutReorder(idx, true);
            }
            return 0;
        }

        if (msg == WM_KEYDOWN) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const int idx = (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
            if (!ClipboardHistoryLegacy::multiMode && wParam == VK_RETURN) {
                useItemWithoutReorder(idx, true);
                return 0;
            }
            if (!ClipboardHistoryLegacy::multiMode && ctrl && wParam == 'C') {
                useItemWithoutReorder(idx, false);
                return 0;
            }
        }

        return listBaseProc
            ? CallWindowProcW(listBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void installNow()
    {
        HWND search = ClipboardHistoryLegacy::searchWnd;
        if (search && IsWindow(search) && !GetPropW(search, L"StarCapV099StableSearch")) {
            // Disable the native cue; stableSearchProc paints the one and only hint.
            SendMessageW(search, EM_SETCUEBANNER, FALSE, (LPARAM)L"");
            searchBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(search, GWLP_WNDPROC, (LONG_PTR)stableSearchProc));
            SetPropW(search, L"StarCapV099StableSearch", (HANDLE)1);
            InvalidateRect(search, nullptr, FALSE);
        }

        HWND list = ClipboardHistoryLegacy::listWnd;
        if (list && IsWindow(list) && !GetPropW(list, L"StarCapV099StableList")) {
            ClipboardHistoryLegacy::hoverIndex = -1;
            listBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)stableListProc));
            SetPropW(list, L"StarCapV099StableList", (HANDLE)1);
            InvalidateRect(list, nullptr, FALSE);
        }
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
            // Install after the earlier v0.9.9 polish hook has finished building its
            // subclass chain, making this stability layer the outermost handler.
            SetTimer(hwnd, INSTALL_TIMER, 30, installTimerProc);
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
