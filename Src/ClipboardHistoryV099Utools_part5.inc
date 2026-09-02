        if (item.type == ItemType::Image) meta = imageMeta(item);
        else if (item.type == ItemType::File) meta = std::format(L"{} 个文件", filePaths(item.data).size());
        else meta = std::format(L"{} 字符", textFromData(item.data).size());

        RECT metaRc{ panel.right - 170, panel.bottom - 24, panel.right - 14, panel.bottom - 7 };
        drawText(dc, meta, metaRc, v099Faint(), smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (expandable) {
            RECT mid{ (panel.left + panel.right) / 2 - 34, panel.bottom - 24, (panel.left + panel.right) / 2 + 34, panel.bottom - 7 };
            drawText(dc, L"展开", mid, v099Muted(), smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void v099DrawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;

        const Item& item = items[visibleItems[dis->itemID]];
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool selected = multiHashes.contains(item.hash);
        const bool hovered = ((int)dis->itemID == hoverIndex);

        RECT rc = dis->rcItem;
        fillRect(dis->hDC, rc, v099Canvas());
        RECT panel = rc;
        panel.left += 2; panel.right -= 9;
        panel.top += 1; panel.bottom -= 1;

        if (hovered) fillRect(dis->hDC, panel, v099Hover());
        if (active || selected) {
            strokeRoundRect(dis->hDC, panel, v099Primary(), 2, 5);
        }
        else {
            RECT sep{ panel.left + 20, panel.bottom - 1, panel.right - 8, panel.bottom };
            fillRect(dis->hDC, sep, v099Border());
        }

        if (item.favorite) {
            RECT fav{ panel.right - 38, panel.top + 8, panel.right - 14, panel.top + 28 };
            drawText(dis->hDC, L"★", fav, v099Primary(), uiFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        bool oversized = false;
        if (item.type == ItemType::Image) {
            RECT preview{ panel.left + 20, panel.top + 12, panel.left + 268, panel.top + 103 };
            fillRoundRect(dis->hDC, preview, RGB(250, 250, 250), 5);
            strokeRoundRect(dis->hDC, preview, v099Border(), 1, 5);
            RECT imageRc = preview; InflateRect(&imageRc, -7, -7);
            drawDibFit(dis->hDC, item, imageRc);

            RECT imageTitle{ preview.right + 18, panel.top + 19, panel.right - 18, panel.top + 41 };
            drawText(dis->hDC, L"图片", imageTitle, v099Text(), boldFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT imageInfo{ preview.right + 18, panel.top + 47, panel.right - 18, panel.top + 69 };
            drawText(dis->hDC, imageMeta(item), imageInfo, v099Muted(), uiFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT hint{ preview.right + 18, panel.top + 75, panel.right - 18, panel.top + 97 };
            drawText(dis->hDC, L"双击即可粘贴", hint, v099Faint(), smallFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            v099DrawBottomMeta(dis->hDC, item, panel, true);
        }
        else if (item.type == ItemType::File) {
            RECT body{ panel.left + 20, panel.top + 10, panel.right - 18, panel.bottom - 26 };
            v099DrawFilePreview(dis->hDC, item, body);
            v099DrawBottomMeta(dis->hDC, item, panel, false);
        }
        else {
            auto previewText = itemPreview(item, oversized);
            RECT body{ panel.left + 20, panel.top + 12, panel.right - 16, panel.bottom - 26 };
            drawText(dis->hDC, previewText, body, v099Text(), uiFont,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            v099DrawBottomMeta(dis->hDC, item, panel, oversized);
        }
    }

    inline void v099Layout(HWND hwnd)
    {
        if (!hwnd) return;
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = std::max(1, (int)rc.right);
        const int h = std::max(1, (int)rc.bottom);

        if (listWnd)
            MoveWindow(listWnd, 10, V099_HEADER_H + 2, std::max(1, w - 20), std::max(1, h - V099_HEADER_H - 8), TRUE);

        if (searchWnd) {
            RECT outer = v099SearchOuterRect(rc);
            MoveWindow(searchWnd, outer.left + 6, outer.top + 5,
                std::max(90, outer.right - outer.left - 12), outer.bottom - outer.top - 10, TRUE);
            ShowWindow(searchWnd, SW_SHOW);
        }

        if (clearWnd) ShowWindow(clearWnd, SW_HIDE);
        v099ApplyRoundRegion(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline void v099SetPinned(HWND hwnd)
    {
        sidePinned = !sidePinned;
        SetWindowPos(hwnd, sidePinned ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline LRESULT CALLBACK v099SearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
