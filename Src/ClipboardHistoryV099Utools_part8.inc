        }

        case WM_SIZE:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            const int width = (int)rc.right;
            const bool widthChanged = std::abs(width - v099LastWidth) > 80;
            v099LastWidth = width;
            v099ApplyRoundRegion(hwnd);
            v099Layout(hwnd);
            if (widthChanged && listWnd && !visibleItems.empty()) {
                InvalidateRect(listWnd, nullptr, TRUE);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORLISTBOX:
        {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, v099Text());
            SetBkColor(dc, v099Canvas());
            return (LRESULT)v099ListBrush;
        }

        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, v099Text());
            SetBkColor(dc, v099Surface());
            return (LRESULT)v099SearchBrush;
        }

        case WM_MEASUREITEM:
        {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == ID_LIST) {
                if (mi->itemID < visibleItems.size())
                    mi->itemHeight = v099ItemHeight(items[visibleItems[mi->itemID]]);
                else
                    mi->itemHeight = 88;
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == ID_LIST) {
                v099DrawListItem(dis);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
        {
            UINT id = LOWORD(wParam);
            UINT code = HIWORD(wParam);
            if (id == ID_SEARCH && code == EN_CHANGE) {
                refreshList();
                return 0;
            }
            if (id == ID_CLEAR_FLOAT && code == BN_CLICKED) {
                clearAll();
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc{}; GetClientRect(hwnd, &rc);

            if (pointIn(v099CloseRect(rc), p)) {
                if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            for (int i = 0; i < 5; ++i) {
                if (pointIn(v099TabRect(rc, i), p)) {
                    setTab((Tab)i);
                    v099Layout(hwnd);
                    return 0;
                }
            }


            break;
        }

        case WM_SETTINGCHANGE:
            return 0;

        case WM_CLOSE:
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            historyWnd = nullptr;
            listWnd = nullptr;
            searchWnd = nullptr;
            clearWnd = nullptr;
            fullWnd = nullptr;
