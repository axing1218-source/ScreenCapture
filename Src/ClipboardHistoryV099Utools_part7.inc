                    if (pointIn(thumb, p)) {
                        v099ScrollDragging = true;
                        v099ScrollDragOffset = p.y - thumb.top;
                    }
                    else {
                        v099ScrollDragging = true;
                        v099ScrollDragOffset = std::max(1, (thumb.bottom - thumb.top) / 2);
                        v099SetScrollFromThumbY(hwnd, p.y, v099ScrollDragOffset);
                    }
                    SetCapture(hwnd);
                    return 0;
                }
            }
        }
        if (msg == WM_MOUSEMOVE && v099ScrollDragging) {
            v099SetScrollFromThumbY(hwnd, GET_Y_LPARAM(lParam), v099ScrollDragOffset);
            return 0;
        }
        if (msg == WM_LBUTTONUP && v099ScrollDragging) {
            v099ScrollDragging = false;
            ReleaseCapture();
            return 0;
        }

        // The native ListBox wheel handler depends on a native WS_VSCROLL bar. The
        // v0.9.9 UI intentionally removed that bar and paints its own floating thumb,
        // so handle wheel input explicitly and move the ListBox top index ourselves.
        if (msg == WM_MOUSEWHEEL) {
            static int wheelRemainder = 0;
            wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = wheelRemainder / WHEEL_DELTA;
            wheelRemainder %= WHEEL_DELTA;
            if (notches != 0 && !visibleItems.empty()) {
                UINT wheelLines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
                int rowsPerNotch = 3;
                if (wheelLines == WHEEL_PAGESCROLL) rowsPerNotch = 5;
                else rowsPerNotch = std::clamp((int)wheelLines, 1, 8);

                const int topIndex = std::max(0, (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
                const int maxIndex = std::max(0, (int)visibleItems.size() - 1);
                const int target = std::clamp(topIndex - notches * rowsPerNotch, 0, maxIndex);
                SendMessageW(hwnd, LB_SETTOPINDEX, target, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            ReleaseCapture();
            return 0;
        }

        LRESULT result = ClipboardHistoryLegacy::listProc(hwnd, msg, wParam, lParam);

        if (msg == WM_PAINT) {
            HDC dc = GetDC(hwnd);
            if (dc) {
                v099DrawFloatingThumb(hwnd, dc);
                ReleaseDC(hwnd, dc);
            }
        }

        if (msg == WM_KEYDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
            msg == WM_RBUTTONUP || msg == WM_CHAR || msg == WM_MOUSEWHEEL) {
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (historyWnd) InvalidateRect(historyWnd, nullptr, FALSE);
        }
        return result;
    }

    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd;
            themeMode = ThemeMode::Light;

            v099EnsureFonts(hwnd);
            v099SyncBrushes();

            listWnd = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_LIST), GetModuleHandleW(nullptr), nullptr);

            searchWnd = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_SEARCH), GetModuleHandleW(nullptr), nullptr);

            clearWnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR_FLOAT), GetModuleHandleW(nullptr), nullptr);

            if (listWnd) {
                SendMessageW(listWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldListProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)v099ListProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldSearchProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(searchWnd, GWLP_WNDPROC, (LONG_PTR)v099SearchProc));
                SendMessageW(searchWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索...");
            }

            refreshList();
            v099ApplyRoundRegion(hwnd);
            v099Layout(hwnd);
            return 0;
        }

        case WM_GETMINMAXINFO:
        {
            auto* p = reinterpret_cast<MINMAXINFO*>(lParam);
            p->ptMinTrackSize.x = 700;
            p->ptMinTrackSize.y = 460;
            return 0;
        }

        case WM_NCHITTEST:
        {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit != HTCLIENT) return hit;

            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);

            if (pointIn(v099CloseRect(rc), p)) return HTCLIENT;

            for (int i = 0; i < 5; ++i)
                if (pointIn(v099TabRect(rc, i), p)) return HTCLIENT;

            if (p.y < V099_TOP_H) return HTCAPTION;
            return HTCLIENT;
