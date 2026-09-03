            oldListProc = nullptr;
            oldSearchProc = nullptr;
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            v099DrawHeader(dc, rc);
            // The outer frame is now drawn by DWM. Do not draw a second GDI RoundRect:
            // the duplicate pixel-snapped outline was the visible gray/jagged border.
            EndPaint(hwnd, &ps);
            return 0;
        }
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void init()
    {
        ClipboardHistoryLegacy::init();
        if (v099UiClassRegistered) return;

        WNDCLASSW wc{};
        wc.lpfnWndProc = historyProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"StarCapClipboardHistoryV099";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            v099UiClassRegistered = true;
    }

    inline void show()
    {
        if (!listenerWnd) init();

        HWND fg = GetForegroundWindow();
        if (fg && fg != historyWnd && fg != listWnd && fg != searchWnd && fg != fullWnd)
            lastForeground = fg;

        if (!historyWnd) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

            const int workW = (int)(work.right - work.left);
            const int workH = (int)(work.bottom - work.top);
            const int ww = std::min(820, std::max(700, workW * 52 / 100));
            const int wh = std::min(620, std::max(500, workH * 68 / 100));
            const int x = work.left + (workW - ww) / 2;
            const int y = work.top + (workH - wh) / 2;

            DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN;

            historyWnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                L"StarCapClipboardHistoryV099", L"StarCap 剪贴板",
                style, x, y, ww, wh, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        }

        if (!historyWnd) return;

        refreshList();
        v099Layout(historyWnd);
        ShowWindow(historyWnd, SW_SHOW);
        SetForegroundWindow(historyWnd);
        if (listWnd) SetFocus(listWnd);
    }

    inline void toggle()
    {
        if (historyWnd && IsWindow(historyWnd) && IsWindowVisible(historyWnd)) {
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(historyWnd, SW_HIDE);
            return;
        }
        show();
    }

    inline void dispose()
    {
        if (v099ListBrush) {
            DeleteObject(v099ListBrush);
            v099ListBrush = nullptr;
        }
        if (v099SearchBrush) {
            DeleteObject(v099SearchBrush);
            v099SearchBrush = nullptr;
        }
        if (v099TitleFont) {
            DeleteObject(v099TitleFont);
            v099TitleFont = nullptr;
        }
        if (v099IconFont) {
            DeleteObject(v099IconFont);
            v099IconFont = nullptr;
        }
        if (v099TabIconFont) {
            DeleteObject(v099TabIconFont);
            v099TabIconFont = nullptr;
        }
        ClipboardHistoryLegacy::dispose();
    }
}
