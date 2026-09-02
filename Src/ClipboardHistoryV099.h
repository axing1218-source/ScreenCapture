#pragma once

// StarCap v0.9.9 clipboard presentation layer.
// The storage/capture/search/restore logic remains in ClipboardHistoryImpl.h.
// This file only replaces the standalone window shell and list rendering with
// a uTools-inspired responsive UI (wide mode + compact pinned mode).

namespace ClipboardHistory
{
    using namespace ClipboardHistoryLegacy;

    inline bool v099UiClassRegistered{ false };
    inline bool v099CompactSearchOpen{ false };
    inline bool v099LastCompact{ false };
    inline HBRUSH v099ListBrush{ nullptr };
    inline RECT v099RestoreRect{};
    inline bool v099HasRestoreRect{ false };

    inline LRESULT CALLBACK historyProc(HWND, UINT, WPARAM, LPARAM);

    inline int v099RailWidth(const RECT& rc)
    {
        const int w = rc.right - rc.left;
        return w >= 720 ? 92 : 0;
    }

    inline bool v099Compact(HWND hwnd = nullptr)
    {
        RECT rc{};
        HWND target = hwnd ? hwnd : historyWnd;
        if (!target || !GetClientRect(target, &rc)) return false;
        return (rc.right - rc.left) < 720;
    }

    inline int v099TopHeight(HWND hwnd = nullptr)
    {
        return v099Compact(hwnd) ? 58 : 136;
    }

    inline void v099SyncBrushes()
    {
        const Theme t = theme();
        if (v099ListBrush) DeleteObject(v099ListBrush);
        v099ListBrush = CreateSolidBrush(t.bg);
        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(t.navBg);
    }

    inline void v099EnsureFonts(HWND hwnd)
    {
        if (uiFont) return;
        UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        auto px = [dpi](int pt) { return -MulDiv(pt, dpi, 72); };
        uiFont = CreateFontW(px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        smallFont = CreateFontW(px(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        boldFont = CreateFontW(px(11), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        emojiFont = CreateFontW(px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
    }

    inline RECT v099WideTitlePill()
    {
        return { 18, 14, 174, 64 };
    }

    inline RECT v099WideTitleClose()
    {
        return { 142, 20, 168, 58 };
    }

    inline RECT v099WideMoreRect(const RECT& client)
    {
        const int contentRight = client.right - v099RailWidth(client);
        return { std::max(180, contentRight - 54), 14, std::max(222, contentRight - 10), 64 };
    }

    inline RECT v099TabRect(const RECT& client, int index)
    {
        const int contentRight = client.right - v099RailWidth(client);
        const int tabW = std::max(92, contentRight / 5);
        int left = index * tabW;
        int right = (index == 4) ? contentRight : std::min(contentRight, left + tabW);
        return { left, 80, right, 136 };
    }

    inline RECT v099RailButtonRect(const RECT& client, int index)
    {
        const int rail = v099RailWidth(client);
        const int railLeft = client.right - rail;
        const int cx = railLeft + rail / 2;
        if (index < 3) {
            const int cy = 188 + index * 90;
            return { cx - 28, cy - 28, cx + 28, cy + 28 };
        }
        const int cy = client.bottom - 190 + (index - 3) * 78;
        return { cx - 24, cy - 24, cx + 24, cy + 24 };
    }

    inline RECT v099CompactButtonRect(const RECT& client, int index)
    {
        // 0 all, 1 favorite, 2 current/count, 3 keyboard/multi, 4 search, 5 close
        const int w = client.right - client.left;
        if (index <= 1) return { index * 70, 0, (index + 1) * 70, 58 };
        if (index == 2) return { std::max(140, w - 250), 0, std::max(198, w - 192), 58 };
        if (index == 3) return { std::max(198, w - 192), 0, std::max(256, w - 134), 58 };
        if (index == 4) return { std::max(256, w - 134), 0, std::max(314, w - 76), 58 };
        return { std::max(314, w - 76), 0, w, 58 };
    }

    inline int v099ItemHeight(const Item& item)
    {
        RECT rc{};
        if (historyWnd) GetClientRect(historyWnd, &rc);
        const bool compact = v099Compact();
        const int contentW = std::max(360, (int)rc.right - v099RailWidth(rc));
        if (item.type == ItemType::Image) return compact ? 165 : 210;
        if (item.type == ItemType::File) {
            const auto n = filePaths(item.data).size();
            return (n > 3) ? (compact ? 154 : 160) : (compact ? 112 : 118);
        }
        std::wstring text = textFromData(item.data);
        int explicitLines = 1;
        for (wchar_t c : text) if (c == L'\n') ++explicitLines;
        const int charsPerLine = std::max(24, (contentW - 72) / (compact ? 12 : 11));
        int wrapped = (int)((text.size() + (size_t)charsPerLine - 1) / (size_t)charsPerLine);
        int lines = std::clamp(std::max(explicitLines, wrapped), 1, 5);
        return std::clamp(44 + lines * (compact ? 25 : 23), 76, compact ? 169 : 159);
    }

    inline void v099DrawFileIcon(HDC dc, int x, int y, const std::filesystem::path& path, const Theme& t)
    {
        RECT page{ x, y, x + 18, y + 23 };
        const auto ext = path.extension().wstring();
        if (_wcsicmp(ext.c_str(), L".pdf") == 0) {
            fillRoundRect(dc, page, RGB(0xe8, 0x43, 0x52), 3);
            drawText(dc, L"P", page, RGB(0xff,0xff,0xff), smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        HBRUSH brush = CreateSolidBrush(t.bg);
        HPEN pen = CreatePen(PS_SOLID, 1, t.textLighter);
        auto oldB = SelectObject(dc, brush);
        auto oldP = SelectObject(dc, pen);
        Rectangle(dc, page.left, page.top, page.right, page.bottom);
        MoveToEx(dc, page.right - 6, page.top, nullptr);
        LineTo(dc, page.right - 6, page.top + 6);
        LineTo(dc, page.right, page.top + 6);
        SelectObject(dc, oldP); SelectObject(dc, oldB);
        DeleteObject(pen); DeleteObject(brush);
    }

    inline void v099DrawFilePreview(HDC dc, const Item& item, RECT body, const Theme& t)
    {
        auto paths = filePaths(item.data);
        if (paths.empty()) {
            drawText(dc, L"文件/文件夹", body, t.text, uiFont, DT_LEFT | DT_TOP | DT_SINGLELINE);
            return;
        }
        const int rowH = 31;
        const int maxRows = std::max(1, (body.bottom - body.top) / rowH);
        const int count = std::min((int)paths.size(), maxRows);
        for (int i = 0; i < count; ++i) {
            std::filesystem::path p(paths[(size_t)i]);
            v099DrawFileIcon(dc, body.left, body.top + i * rowH + 1, p, t);
            RECT tr{ body.left + 28, body.top + i * rowH, body.right, body.top + (i + 1) * rowH };
            auto name = p.filename().wstring();
            if (name.empty()) name = paths[(size_t)i];
            drawText(dc, name, tr, t.text, uiFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    inline void v099DrawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;
        const Theme t = theme();
        const Item& item = items[visibleItems[dis->itemID]];
        RECT rc = dis->rcItem;
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool multiSelected = multiHashes.contains(item.hash);
        const bool hovered = ((int)dis->itemID == hoverIndex);
        const bool compact = v099Compact();

        fillRect(dis->hDC, rc, t.bg);
        RECT card = rc;
        InflateRect(&card, -3, 0);
        if (hovered || active || multiSelected) fillRect(dis->hDC, card, selectionBg());
        if (active || multiSelected) {
            strokeRoundRect(dis->hDC, card, t.primary, 2, 4);
        }
        else {
            RECT sep{ card.left + (compact ? 24 : 38), card.bottom - 1, card.right - 12, card.bottom };
            fillRect(dis->hDC, sep, t.textBgLighter);
        }

        RECT body{ card.left + (compact ? 24 : 38), card.top + 9,
            card.right - (compact ? 20 : 28), card.bottom - 31 };
        bool oversized = false;
        if (item.type == ItemType::Image) {
            RECT imageRc = body;
            imageRc.left += compact ? 16 : 36;
            imageRc.right -= compact ? 16 : 36;
            drawDibFit(dis->hDC, item, imageRc);
        }
        else if (item.type == ItemType::File) {
            v099DrawFilePreview(dis->hDC, item, body, t);
        }
        else {
            auto preview = itemPreview(item, oversized);
            drawText(dis->hDC, preview, body, t.text, uiFont,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        const int leftPad = compact ? 24 : 38;
        RECT timeRc{ card.left + leftPad, card.bottom - 29, card.left + leftPad + 160, card.bottom - 3 };
        drawText(dis->hDC, relativeTime(item.updated), timeRc, t.textLighter, smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        bool expandable = item.type == ItemType::Image || item.type == ItemType::File || oversized;
        if (expandable) {
            RECT er = expandRect(card);
            drawText(dis->hDC, L"⌄  展开", er, t.textLighter, smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        std::wstring meta;
        if (item.type == ItemType::Image) meta = imageMeta(item);
        else if (item.type == ItemType::File) meta = std::format(L"{} 个文件", filePaths(item.data).size());
        else meta = std::format(L"{} 字符", textFromData(item.data).size());
        RECT metaRc{ card.right - (compact ? 178 : 260), card.bottom - 29, card.right - 56, card.bottom - 3 };
        drawText(dis->hDC, meta, metaRc, t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT indexRc{ card.right - 51, card.bottom - 29, card.right - 13, card.bottom - 3 };
        std::wstring index = std::to_wstring(dis->itemID + 1);
        if (multiSelected) index = L"✓ " + index;
        drawText(dis->hDC, index, multiSelected ? t.primary : t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        if (item.favorite) {
            RECT favRc{ card.right - 50, card.top + 6, card.right - 14, card.top + 31 };
            drawText(dis->hDC, L"★", favRc, t.primary, uiFont,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void v099DrawMagnifier(HDC dc, const RECT& r, COLORREF color)
    {
        HPEN p = CreatePen(PS_SOLID, 2, color);
        auto oldP = SelectObject(dc, p);
        auto oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        int cx = (r.left + r.right) / 2 - 3, cy = (r.top + r.bottom) / 2 - 2;
        Ellipse(dc, cx - 7, cy - 7, cx + 7, cy + 7);
        MoveToEx(dc, cx + 5, cy + 5, nullptr); LineTo(dc, cx + 12, cy + 12);
        SelectObject(dc, oldB); SelectObject(dc, oldP); DeleteObject(p);
    }

    inline void v099DrawWideHeader(HDC dc, const RECT& client)
    {
        const Theme t = theme();
        const int rail = v099RailWidth(client);
        const int contentRight = client.right - rail;
        RECT header{ 0,0,contentRight,80 };
        RECT tabsBg{ 0,80,contentRight,136 };
        fillRect(dc, header, t.navBg);
        fillRect(dc, tabsBg, t.navBg);

        RECT pill = v099WideTitlePill();
        fillRoundRect(dc, pill, t.navHover, 24);
        RECT iconRc{ pill.left + 12,pill.top + 8,pill.left + 42,pill.bottom - 8 };
        fillRoundRect(dc, iconRc, t.primary, 5);
        drawText(dc, L"▣", iconRc, t.bg, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT titleRc{ pill.left + 48,pill.top,pill.right - 28,pill.bottom };
        drawText(dc, L"剪贴板", titleRc, t.text, boldFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT closeRc = v099WideTitleClose();
        drawText(dc, L"×", closeRc, t.textLighter, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (!multiMode) {
            RECT searchVisual{ 194,15,std::max(285,contentRight - 64),63 };
            // Keep the search area visually flat like uTools; a subtle hover-tone rectangle
            // gives the native EDIT a clean background without a Windows border.
            fillRoundRect(dc, searchVisual, t.navBg, 8);
            RECT more = v099WideMoreRect(client);
            drawText(dc, L"⋮", more, t.textLighter, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else {
            RECT multiBg{ 194,15,std::max(360,contentRight - 64),63 };
            fillRoundRect(dc, multiBg, t.bg, 8);
            RECT count{ multiBg.left + 12,multiBg.top,multiBg.left + 120,multiBg.bottom };
            drawText(dc, std::format(L"已选 {}", multiHashes.size()), count, t.primary, boldFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ multiBg.right - 220,19,multiBg.right - 154,59 };
            RECT paste{ multiBg.right - 148,19,multiBg.right - 82,59 };
            RECT exit{ multiBg.right - 76,19,multiBg.right - 10,59 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 6);
            drawText(dc, L"复制", copy, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = { L"▣  全部", L"Tᵀ  文本", L"▧  图像", L"▱  文件",
            favCount ? std::format(L"★  收藏 ({})", favCount) : L"★  收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = v099TabRect(client, i);
            const bool selected = (int)activeTab == i;
            if (selected) {
                RECT selectedRc = r; selectedRc.left += 10; selectedRc.right -= 10;
                fillRoundRect(dc, selectedRc, t.bg, 8);
                RECT join{ selectedRc.left, selectedRc.bottom - 9, selectedRc.right, selectedRc.bottom };
                fillRect(dc, join, t.bg);
            }
            drawText(dc, labels[i], r, selected ? t.text : t.textLighter,
                selected ? boldFont : uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT sep{ 0,135,contentRight,136 };
        fillRect(dc, sep, t.textBgLighter);

        // Right rail, including a small branded clipboard tile at the top.
        RECT railRc{ contentRight,0,client.right,client.bottom };
        fillRect(dc, railRc, t.navBg);
        RECT appTile{ contentRight + 20,17,client.right - 20,67 };
        fillRoundRect(dc, appTile, t.primary, 6);
        drawText(dc, L"▣→", appTile, t.bg, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const COLORREF circle = t.primary;
        const COLORREF circleGlyph = useDarkTheme() ? RGB(0x16,0x2b,0x3a) : RGB(0xff,0xff,0xff);
        const COLORREF lowerGlyph = t.textLighter;
        for (int i = 0; i < 3; ++i) {
            RECT r = v099RailButtonRect(client, i);
            HBRUSH b = CreateSolidBrush(circle);
            auto oldB = SelectObject(dc, b);
            auto oldP = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldP); SelectObject(dc, oldB); DeleteObject(b);
            drawRailGlyph(dc, i, r, circleGlyph);
        }
        for (int i = 3; i < 6; ++i) drawRailGlyph(dc, i, v099RailButtonRect(client, i), lowerGlyph);
    }

    inline void v099DrawCompactHeader(HDC dc, const RECT& client)
    {
        const Theme t = theme();
        RECT top{ 0,0,client.right,58 };
        fillRect(dc, top, t.navBg);

        RECT all = v099CompactButtonRect(client, 0);
        RECT fav = v099CompactButtonRect(client, 1);
        if (activeTab == Tab::All) fillRect(dc, all, t.bg);
        if (activeTab == Tab::Favorite) fillRect(dc, fav, t.bg);
        drawRailGlyph(dc, 0, all, activeTab == Tab::All ? t.text : t.textLighter);
        drawText(dc, L"★", fav, activeTab == Tab::Favorite ? t.text : t.textLighter, boldFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (!v099CompactSearchOpen && !multiMode) {
            RECT countRc = v099CompactButtonRect(client, 2);
            int idx = currentListIndex();
            std::wstring count = idx >= 0 ? std::to_wstring(idx + 1) : L"0";
            RECT circle{ (countRc.left + countRc.right) / 2 - 17,12,
                (countRc.left + countRc.right) / 2 + 17,46 };
            HBRUSH b = CreateSolidBrush(t.navHover);
            auto oldB = SelectObject(dc, b); auto oldP = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, circle.left,circle.top,circle.right,circle.bottom);
            SelectObject(dc, oldP); SelectObject(dc, oldB); DeleteObject(b);
            drawText(dc, count, circle, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT keyRc = v099CompactButtonRect(client, 3);
            drawText(dc, L"⌨", keyRc, t.text, emojiFont ? emojiFont : uiFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT searchRc = v099CompactButtonRect(client, 4);
            v099DrawMagnifier(dc, searchRc, t.text);
        }
        else if (multiMode) {
            RECT mid{ all.right + 4,6,v099CompactButtonRect(client,5).left - 4,52 };
            drawText(dc, std::format(L"已选 {}   Ctrl+C 复制   Enter 粘贴", multiHashes.size()), mid,
                t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        RECT closeRc = v099CompactButtonRect(client, 5);
        drawText(dc, L"×", closeRc, t.text, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT sep{ 0,57,client.right,58 }; fillRect(dc, sep, t.textBgLighter);
    }

    inline void v099DrawWindow(HDC dc, const RECT& client)
    {
        fillRect(dc, client, theme().bg);
        if (v099Compact()) v099DrawCompactHeader(dc, client);
        else v099DrawWideHeader(dc, client);
        RECT frame{ 0,0,std::max(1,(int)client.right - 1),std::max(1,(int)client.bottom - 1) };
        strokeRoundRect(dc, frame, theme().textBgLighter, 1, 8);
    }

    inline void v099Layout(HWND hwnd)
    {
        if (!hwnd) return;
        RECT rc{}; GetClientRect(hwnd, &rc);
        const bool compact = v099Compact(hwnd);
        const int rail = v099RailWidth(rc);
        const int contentW = std::max(1, (int)rc.right - rail);
        const int top = compact ? 58 : 136;
        if (listWnd) MoveWindow(listWnd, 0, top, contentW, std::max(1, (int)rc.bottom - top), TRUE);

        bool showSearch = !multiMode && (!compact || v099CompactSearchOpen);
        if (searchWnd) {
            if (showSearch) {
                if (compact) {
                    int left = 78, right = std::max(left + 120, v099CompactButtonRect(rc, 5).left - 8);
                    MoveWindow(searchWnd, left, 11, std::max(120, right - left), 36, TRUE);
                }
                else {
                    int left = 204, right = std::max(left + 160, contentW - 70);
                    MoveWindow(searchWnd, left, 19, std::max(160, right - left), 38, TRUE);
                }
                ShowWindow(searchWnd, SW_SHOW);
            }
            else ShowWindow(searchWnd, SW_HIDE);
        }
        if (clearWnd) ShowWindow(clearWnd, SW_HIDE);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    inline void v099SetCompactSearch(bool open)
    {
        v099CompactSearchOpen = open;
        if (!open && searchWnd) {
            SetWindowTextW(searchWnd, L"");
            refreshList();
        }
        v099Layout(historyWnd);
        if (open && searchWnd) SetFocus(searchWnd);
        else if (listWnd) SetFocus(listWnd);
    }

    inline void v099TogglePinned(HWND hwnd)
    {
        if (!hwnd) return;
        sidePinned = !sidePinned;
        if (sidePinned) {
            GetWindowRect(hwnd, &v099RestoreRect);
            v099HasRestoreRect = true;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{ sizeof(MONITORINFO) }; GetMonitorInfoW(mon, &mi);
            const int ww = std::min(560, mi.rcWork.right - mi.rcWork.left);
            const int wh = std::min(820, std::max(480, mi.rcWork.bottom - mi.rcWork.top - 40));
            const int x = mi.rcWork.right - ww - 8;
            const int y = mi.rcWork.top + 20;
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, ww, wh, SWP_SHOWWINDOW);
        }
        else {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (v099HasRestoreRect) {
                SetWindowPos(hwnd, HWND_TOP, v099RestoreRect.left, v099RestoreRect.top,
                    v099RestoreRect.right - v099RestoreRect.left,
                    v099RestoreRect.bottom - v099RestoreRect.top, SWP_SHOWWINDOW);
            }
        }
        v099Layout(hwnd);
    }

    inline LRESULT CALLBACK v099SearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE && v099Compact() && v099CompactSearchOpen) {
                v099SetCompactSearch(false); return 0;
            }
            if (wParam == VK_RETURN) { useListItem(currentListIndex(), true); return 0; }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                int idx = currentListIndex();
                int next = idx + (wParam == VK_DOWN ? 1 : -1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                if (listWnd) {
                    SendMessageW(listWnd, LB_SETCURSEL, next, 0);
                    SendMessageW(listWnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                    SetFocus(listWnd);
                }
                return 0;
            }
        }
        return ClipboardHistoryLegacy::searchProc(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK v099ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CHAR && wParam >= 0x20 && wParam != 0x7f && v099Compact() && !multiMode) {
            if (!v099CompactSearchOpen) {
                v099CompactSearchOpen = true;
                v099Layout(historyWnd);
            }
        }
        if (msg == WM_KEYDOWN && v099Compact() && !multiMode) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_OEM_2 || (ctrl && (wParam == 'F' || wParam == 'L'))) {
                v099CompactSearchOpen = true;
                v099Layout(historyWnd);
            }
        }
        LRESULT result = ClipboardHistoryLegacy::listProc(hwnd, msg, wParam, lParam);
        if (msg == WM_KEYDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK || msg == WM_CHAR) {
            v099Layout(historyWnd);
            if (historyWnd) InvalidateRect(historyWnd, nullptr, FALSE);
        }
        return result;
    }

    inline void v099CycleTheme()
    {
        cycleThemeMode();
        v099SyncBrushes();
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
    }

    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd;
            v099EnsureFonts(hwnd);
            v099SyncBrushes();
            listWnd = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_LIST), GetModuleHandleW(nullptr), nullptr);
            searchWnd = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_SEARCH), GetModuleHandleW(nullptr), nullptr);
            clearWnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR_FLOAT), GetModuleHandleW(nullptr), nullptr);
            if (listWnd) {
                SendMessageW(listWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)v099ListProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(searchWnd, GWLP_WNDPROC, (LONG_PTR)v099SearchProc));
                SendMessageW(searchWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(7, 7));
            }
            v099LastCompact = v099Compact(hwnd);
            refreshList();
            v099Layout(hwnd);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto* p = reinterpret_cast<MINMAXINFO*>(lParam);
            p->ptMinTrackSize.x = 520;
            p->ptMinTrackSize.y = 420;
            return 0;
        }
        case WM_NCHITTEST:
        {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit != HTCLIENT) return hit;
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (v099Compact(hwnd)) {
                for (int i = 0; i < 6; ++i) if (pointIn(v099CompactButtonRect(rc, i), p)) return HTCLIENT;
                if (p.y < 58) return HTCAPTION;
            }
            else {
                if (pointIn(v099WideTitleClose(), p) || pointIn(v099WideMoreRect(rc), p)) return HTCLIENT;
                for (int i = 0; i < 5; ++i) if (pointIn(v099TabRect(rc, i), p)) return HTCLIENT;
                if (p.y < 80) return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_SIZE:
        {
            const bool nowCompact = v099Compact(hwnd);
            v099Layout(hwnd);
            if (nowCompact != v099LastCompact) {
                v099LastCompact = nowCompact;
                if (!nowCompact) v099CompactSearchOpen = false;
                refreshList();
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)wParam, theme().text);
            SetBkColor((HDC)wParam, theme().bg);
            return (LRESULT)v099ListBrush;
        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, theme().text);
            SetBkColor(dc, theme().navBg);
            return (LRESULT)editBrush;
        }
        case WM_MEASUREITEM:
        {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == ID_LIST) {
                if (mi->itemID < visibleItems.size()) mi->itemHeight = v099ItemHeight(items[visibleItems[mi->itemID]]);
                else mi->itemHeight = 88;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == ID_LIST) { v099DrawListItem(dis); return TRUE; }
            break;
        }
        case WM_COMMAND:
        {
            UINT id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == ID_SEARCH && code == EN_CHANGE) { refreshList(); return 0; }
            if (id == ID_CLEAR_FLOAT && code == BN_CLICKED) { clearAll(); return 0; }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc{}; GetClientRect(hwnd, &rc);
            if (v099Compact(hwnd)) {
                if (pointIn(v099CompactButtonRect(rc, 0), p)) { setTab(Tab::All); v099Layout(hwnd); return 0; }
                if (pointIn(v099CompactButtonRect(rc, 1), p)) { setTab(Tab::Favorite); v099Layout(hwnd); return 0; }
                if (pointIn(v099CompactButtonRect(rc, 3), p) && !v099CompactSearchOpen) {
                    toggleMultiCurrent(); v099Layout(hwnd); return 0;
                }
                if (pointIn(v099CompactButtonRect(rc, 4), p) && !multiMode) {
                    v099SetCompactSearch(!v099CompactSearchOpen); return 0;
                }
                if (pointIn(v099CompactButtonRect(rc, 5), p)) {
                    if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
                    ShowWindow(hwnd, SW_HIDE); return 0;
                }
            }
            else {
                if (pointIn(v099WideTitleClose(), p)) {
                    if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
                    ShowWindow(hwnd, SW_HIDE); return 0;
                }
                if (pointIn(v099WideMoreRect(rc), p) && !multiMode) { v099CycleTheme(); return 0; }
                const int rail = v099RailWidth(rc);
                if (rail && p.x >= rc.right - rail) {
                    for (int i = 0; i < 6; ++i) {
                        if (!pointIn(v099RailButtonRect(rc, i), p)) continue;
                        if (i == 0) {
                            multiMode = !multiMode;
                            if (!multiMode) multiHashes.clear();
                            v099Layout(hwnd);
                            InvalidateRect(hwnd, nullptr, TRUE);
                            if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                        }
                        else if (i == 1) useListItem(currentListIndex(), true);
                        else if (i == 2) v099TogglePinned(hwnd);
                        else if (i == 3) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                        else if (i == 4) WinSetting::init();
                        else if (i == 5) clearAll();
                        return 0;
                    }
                }
                for (int i = 0; i < 5; ++i) {
                    if (pointIn(v099TabRect(rc, i), p)) { setTab((Tab)i); v099Layout(hwnd); return 0; }
                }
                if (multiMode && p.y < 80) {
                    const int contentRight = rc.right - v099RailWidth(rc);
                    RECT multiBg{ 194,15,std::max(360,contentRight - 64),63 };
                    RECT copy{ multiBg.right - 220,19,multiBg.right - 154,59 };
                    RECT paste{ multiBg.right - 148,19,multiBg.right - 82,59 };
                    RECT exit{ multiBg.right - 76,19,multiBg.right - 10,59 };
                    if (pointIn(copy, p)) { copyMulti(false); return 0; }
                    if (pointIn(paste, p)) { copyMulti(true); return 0; }
                    if (pointIn(exit, p)) {
                        multiMode = false; multiHashes.clear(); multiAnchor = -1;
                        v099Layout(hwnd); InvalidateRect(hwnd, nullptr, TRUE);
                        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                        return 0;
                    }
                }
            }
            break;
        }
        case WM_SETTINGCHANGE:
            if (themeMode == ThemeMode::System) {
                applyThemeNow(); v099SyncBrushes(); InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CLOSE:
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:
            historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr;
            fullWnd = nullptr; oldListProc = nullptr; oldSearchProc = nullptr;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            v099DrawWindow(dc, rc);
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
        if (RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) v099UiClassRegistered = true;
    }

    inline void show()
    {
        if (!listenerWnd) init();
        HWND fg = GetForegroundWindow();
        if (fg && fg != historyWnd && fg != listWnd && fg != searchWnd && fg != fullWnd) lastForeground = fg;
        if (!historyWnd) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int ww = std::min(1204, std::max(760, (int)(work.right - work.left) - 120));
            int wh = std::min(904, std::max(560, (int)(work.bottom - work.top) - 120));
            int x = work.left + ((work.right - work.left) - ww) / 2;
            int y = work.top + ((work.bottom - work.top) - wh) / 2;
            DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN;
            historyWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"StarCapClipboardHistoryV099", L"StarCap 剪贴板",
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
        if (v099ListBrush) { DeleteObject(v099ListBrush); v099ListBrush = nullptr; }
        ClipboardHistoryLegacy::dispose();
    }
}
