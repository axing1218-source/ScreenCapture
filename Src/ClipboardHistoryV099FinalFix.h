#pragma once

// Final v0.9.9 clipboard cleanup layer.
// 1) Adds a visible top-row Clear action without bringing back the heavy toolbar.
// 2) Forces the ListBox's unused client area to paint with the clipboard canvas,
//    preventing stale rows / desktop transparency when a filtered tab has few or
//    zero records (most visible in Favorites).
namespace ClipboardHistoryV099FinalFix
{
    inline WNDPROC historyBaseProc{ nullptr };
    inline WNDPROC listBaseProc{ nullptr };
    inline HWINEVENTHOOK showHook{ nullptr };
    static constexpr UINT_PTR INSTALL_TIMER = 0xC19D;

    inline RECT clearRect(HWND hwnd)
    {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        return { std::max(190, (int)rc.right - 72), 18,
                 std::max(244, (int)rc.right - 20), 50 };
    }

    inline void layoutSearch(HWND hwnd)
    {
        if (!hwnd || !ClipboardHistoryLegacy::searchWnd ||
            !IsWindow(ClipboardHistoryLegacy::searchWnd)) return;

        RECT clear = clearRect(hwnd);
        const int left = 186;
        const int top = 21;
        const int right = std::max(left + 90, clear.left - 10);
        MoveWindow(ClipboardHistoryLegacy::searchWnd, left, top,
            std::max(90, right - left), 26, TRUE);
    }

    inline void drawClearAction(HWND hwnd)
    {
        HDC dc = GetDC(hwnd);
        if (!dc) return;

        RECT r = clearRect(hwnd);
        // Cover the tail of the old full-width search surface, then keep the
        // action intentionally flat so it matches the lightweight uTools header.
        ClipboardHistoryLegacy::fillRect(dc, r, ClipboardHistory::v099Canvas());
        ClipboardHistoryLegacy::drawText(dc, L"清空", r,
            RGB(118, 123, 130), ClipboardHistoryLegacy::smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ReleaseDC(hwnd, dc);
    }

    inline void paintUnusedListArea(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) return;
        HDC dc = GetDC(hwnd);
        if (!dc) return;

        RECT client{};
        GetClientRect(hwnd, &client);
        int usedBottom = 0;
        const int count = (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
        if (count > 0) {
            RECT last{};
            if (SendMessageW(hwnd, LB_GETITEMRECT, count - 1, (LPARAM)&last) != LB_ERR)
                usedBottom = std::clamp((int)last.bottom, 0, (int)client.bottom);
        }

        if (usedBottom < client.bottom) {
            RECT blank{ 0, usedBottom, client.right, client.bottom };
            ClipboardHistoryLegacy::fillRect(dc, blank, ClipboardHistory::v099Canvas());
        }
        ReleaseDC(hwnd, dc);
    }

    inline LRESULT CALLBACK finalListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_ERASEBKGND) {
            HDC dc = (HDC)wParam;
            RECT rc{};
            GetClientRect(hwnd, &rc);
            ClipboardHistoryLegacy::fillRect(dc, rc, ClipboardHistory::v099Canvas());
            return 1;
        }

        LRESULT result = listBaseProc
            ? CallWindowProcW(listBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_PAINT) {
            // Owner-draw rows only cover their own rectangles. Explicitly paint
            // everything after the last filtered item so old rows cannot remain
            // visible and an empty tab never exposes the desktop behind the window.
            paintUnusedListArea(hwnd);
        }
        else if (msg == LB_RESETCONTENT || msg == WM_SHOWWINDOW) {
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return result;
    }

    inline LRESULT CALLBACK finalHistoryProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCHITTEST) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            if (ClipboardHistoryLegacy::pointIn(clearRect(hwnd), p)) return HTCLIENT;
        }

        if (msg == WM_LBUTTONDOWN) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (ClipboardHistoryLegacy::pointIn(clearRect(hwnd), p)) {
                // clearAll already presents the required warning/confirmation and
                // explicitly states that Favorites are included.
                ClipboardHistoryLegacy::clearAll();
                if (ClipboardHistoryLegacy::listWnd)
                    InvalidateRect(ClipboardHistoryLegacy::listWnd, nullptr, TRUE);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        LRESULT result = historyBaseProc
            ? CallWindowProcW(historyBaseProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_SIZE || msg == WM_SHOWWINDOW || msg == WM_WINDOWPOSCHANGED) {
            layoutSearch(hwnd);
            if (ClipboardHistoryLegacy::listWnd)
                InvalidateRect(ClipboardHistoryLegacy::listWnd, nullptr, TRUE);
        }
        if (msg == WM_PAINT) {
            layoutSearch(hwnd);
            drawClearAction(hwnd);
        }
        return result;
    }

    inline void installNow()
    {
        HWND history = ClipboardHistoryLegacy::historyWnd;
        if (history && IsWindow(history) && !GetPropW(history, L"StarCapV099FinalHistory")) {
            historyBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(history, GWLP_WNDPROC, (LONG_PTR)finalHistoryProc));
            if (historyBaseProc) {
                SetPropW(history, L"StarCapV099FinalHistory", (HANDLE)1);
                layoutSearch(history);
                InvalidateRect(history, nullptr, FALSE);
            }
        }

        HWND list = ClipboardHistoryLegacy::listWnd;
        if (list && IsWindow(list) && !GetPropW(list, L"StarCapV099FinalList")) {
            listBaseProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(list, GWLP_WNDPROC, (LONG_PTR)finalListProc));
            if (listBaseProc) {
                SetPropW(list, L"StarCapV099FinalList", (HANDLE)1);
                InvalidateRect(list, nullptr, TRUE);
            }
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
            // PointerFix installs at 90 ms. Install last so this background/header
            // layer wraps the already-stable interaction chain.
            SetTimer(hwnd, INSTALL_TIMER, 160, installTimerProc);
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
