#pragma once

// StarCap v0.9.9 clipboard presentation layer - clean rebuild.
// Storage, capture, filtering, restore, context menu and keyboard behavior stay
// in ClipboardHistoryImpl.h. This file owns only the standalone clipboard UI.

namespace ClipboardHistory
{
    using namespace ClipboardHistoryLegacy;

    inline bool v099UiClassRegistered{ false };
    inline HBRUSH v099ListBrush{ nullptr };
    inline HBRUSH v099SearchBrush{ nullptr };
    inline HFONT v099TitleFont{ nullptr };
    inline int v099LastWidth{ 0 };

    static constexpr int V099_TOP_H = 70;
    static constexpr int V099_TABS_H = 50;
    static constexpr int V099_HEADER_H = V099_TOP_H + V099_TABS_H;

    inline LRESULT CALLBACK historyProc(HWND, UINT, WPARAM, LPARAM);

    inline COLORREF v099Canvas()      { return RGB(254, 254, 254); }
    inline COLORREF v099Card()        { return RGB(255, 255, 255); }
    inline COLORREF v099Surface()     { return RGB(246, 247, 249); }
    inline COLORREF v099Border()      { return RGB(229, 231, 235); }
    inline COLORREF v099Text()        { return RGB(32, 33, 36); }
    inline COLORREF v099Muted()       { return RGB(112, 117, 124); }
    inline COLORREF v099Faint()       { return RGB(154, 160, 166); }
    inline COLORREF v099Primary()     { return RGB(88, 112, 255); }
    inline COLORREF v099PrimarySoft() { return RGB(240, 243, 255); }
    inline COLORREF v099Hover()       { return RGB(248, 249, 251); }
    inline COLORREF v099Selected()    { return RGB(245, 248, 255); }

    inline void v099SyncBrushes()
    {
        if (v099ListBrush) DeleteObject(v099ListBrush);
        if (v099SearchBrush) DeleteObject(v099SearchBrush);
        v099ListBrush = CreateSolidBrush(v099Canvas());
        v099SearchBrush = CreateSolidBrush(v099Surface());

        // Legacy searchProc uses editBrush for the EDIT control.
        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(v099Surface());
    }

    inline void v099EnsureFonts(HWND hwnd)
    {
        if (uiFont) return;
        UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        auto px = [dpi](int pt) { return -MulDiv(pt, dpi, 72); };

        uiFont = CreateFontW(px(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        smallFont = CreateFontW(px(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        boldFont = CreateFontW(px(10), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        emojiFont = CreateFontW(px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
        v099TitleFont = CreateFontW(px(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }

    inline RECT v099CloseRect(const RECT& client)
    {
        const int r = (int)client.right - 18;
        return { r - 34, 18, r, 52 };
    }

    inline RECT v099ClearRect(const RECT& client)
    {
        RECT close = v099CloseRect(client);
        return { close.left - 66, 18, close.left - 8, 52 };
    }

    inline RECT v099PinRect(const RECT& client)
    {
        RECT clear = v099ClearRect(client);
        return { clear.left - 66, 18, clear.left - 8, 52 };
    }

    inline RECT v099MultiRect(const RECT& client)
    {
        RECT pin = v099PinRect(client);
        return { pin.left - 66, 18, pin.left - 8, 52 };
    }

    inline RECT v099SearchOuterRect(const RECT& client)
    {
        RECT multi = v099MultiRect(client);
        return { 154, 17, std::max(314, multi.left - 14), 53 };
    }

    inline RECT v099TabRect(const RECT& client, int index)
    {
        static constexpr int widths[5] = { 72, 78, 78, 78, 94 };
        int x = 20;
        for (int i = 0; i < index; ++i) x += widths[i] + 8;
        return { x, 78, x + widths[index], 112 };
    }

    inline void v099DrawMagnifier(HDC dc, RECT r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        auto oldP = SelectObject(dc, pen);
        auto oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int cx = r.left + 13;
        const int cy = (r.top + r.bottom) / 2 - 1;
        Ellipse(dc, cx - 5, cy - 5, cx + 5, cy + 5);
        MoveToEx(dc, cx + 4, cy + 4, nullptr);
        LineTo(dc, cx + 9, cy + 9);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(pen);
    }

    inline void v099DrawButton(HDC dc, RECT r, const std::wstring& label, bool active = false, bool danger = false)
    {
        const COLORREF bg = active ? v099PrimarySoft() : v099Surface();
        const COLORREF fg = danger ? RGB(205, 62, 62) : (active ? v099Primary() : v099Muted());
        fillRoundRect(dc, r, bg, 8);
        if (active) strokeRoundRect(dc, r, RGB(210, 219, 255), 1, 8);
        drawText(dc, label, r, fg, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void v099DrawHeader(HDC dc, const RECT& client)
    {
        fillRect(dc, client, v099Canvas());

        RECT logo{ 20, 18, 54, 52 };
        fillRoundRect(dc, logo, v099Primary(), 8);
        drawText(dc, L"▣", logo, RGB(255,255,255), boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT title{ 64, 11, 146, 39 };
        drawText(dc, L"剪贴板", title, v099Text(), v099TitleFont ? v099TitleFont : boldFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT count{ 64, 36, 146, 58 };
        drawText(dc, std::format(L"{} 条记录", items.size()), count, v099Faint(), smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (!multiMode) {
            RECT search = v099SearchOuterRect(client);
            fillRoundRect(dc, search, v099Surface(), 10);
            v099DrawMagnifier(dc, search, v099Faint());
        }
        else {
            RECT bar = v099SearchOuterRect(client);
            fillRoundRect(dc, bar, v099PrimarySoft(), 10);
            RECT status{ bar.left + 14, bar.top, bar.left + 110, bar.bottom };
            drawText(dc, std::format(L"已选 {}", multiHashes.size()), status, v099Primary(), boldFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT copy{ bar.right - 190, bar.top + 3, bar.right - 132, bar.bottom - 3 };
            RECT paste{ bar.right - 126, bar.top + 3, bar.right - 68, bar.bottom - 3 };
            RECT exit{ bar.right - 62, bar.top + 3, bar.right - 6, bar.bottom - 3 };
            v099DrawButton(dc, copy, L"复制");
            v099DrawButton(dc, paste, L"粘贴", true);
            v099DrawButton(dc, exit, L"退出");
        }

        v099DrawButton(dc, v099MultiRect(client), L"多选", multiMode);
        v099DrawButton(dc, v099PinRect(client), sidePinned ? L"已置顶" : L"置顶", sidePinned);
        v099DrawButton(dc, v099ClearRect(client), L"清空");
        v099DrawButton(dc, v099CloseRect(client), L"×", false, true);

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = {
            L"全部", L"文本", L"图像", L"文件",
            favCount ? std::format(L"收藏 {}", favCount) : L"收藏"
        };

        for (int i = 0; i < 5; ++i) {
            RECT tab = v099TabRect(client, i);
            const bool selected = (int)activeTab == i;
            if (selected) fillRoundRect(dc, tab, v099PrimarySoft(), 10);
            drawText(dc, labels[i], tab, selected ? v099Primary() : v099Muted(),
                selected ? boldFont : uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT line{ 0, V099_HEADER_H - 1, client.right, V099_HEADER_H };
        fillRect(dc, line, v099Border());
    }

    inline int v099TextLines(const Item& item, int width)
    {
        std::wstring text = textFromData(item.data);
        int explicitLines = 1;
        for (wchar_t c : text) if (c == L'\n') ++explicitLines;
        const int charsPerLine = std::max(22, (width - 72) / 12);
        const int wrapped = (int)((text.size() + (size_t)charsPerLine - 1) / (size_t)charsPerLine);
        return std::clamp(std::max(explicitLines, wrapped), 1, 4);
    }

    inline int v099ItemHeight(const Item& item)
    {
        RECT rc{};
        int width = 760;
        if (listWnd && GetClientRect(listWnd, &rc)) width = std::max(520, (int)rc.right);
        if (item.type == ItemType::Image) return 196;
        if (item.type == ItemType::File) {
            const int n = std::max(1, (int)filePaths(item.data).size());
            const int rows = std::min(n, 3);
            return 94 + rows * 30 + (n > 3 ? 16 : 0);
        }
        return 88 + v099TextLines(item, width) * 22;
    }

    inline void v099DrawFileIcon(HDC dc, int x, int y, const std::filesystem::path& path)
    {
        RECT page{ x, y, x + 20, y + 24 };
        const auto ext = path.extension().wstring();

        if (_wcsicmp(ext.c_str(), L".pdf") == 0) {
            fillRoundRect(dc, page, RGB(232, 67, 82), 4);
            drawText(dc, L"P", page, RGB(255,255,255), smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        fillRoundRect(dc, page, v099Surface(), 4);
        strokeRoundRect(dc, page, v099Border(), 1, 4);
        RECT mark{ page.left + 5, page.top + 6, page.right - 5, page.bottom - 6 };
        drawText(dc, L"·", mark, v099Muted(), boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void v099DrawFilePreview(HDC dc, const Item& item, RECT body)
    {
        auto paths = filePaths(item.data);
        if (paths.empty()) {
            drawText(dc, L"文件或文件夹", body, v099Text(), uiFont, DT_LEFT | DT_TOP | DT_SINGLELINE);
            return;
        }

        const int rowH = 30;
        const int maxRows = std::max(1, (body.bottom - body.top) / rowH);
        const int count = std::min((int)paths.size(), std::min(3, maxRows));
        for (int i = 0; i < count; ++i) {
            std::filesystem::path p(paths[(size_t)i]);
            v099DrawFileIcon(dc, body.left, body.top + i * rowH + 2, p);
            RECT tr{ body.left + 30, body.top + i * rowH, body.right, body.top + (i + 1) * rowH };
            auto name = p.filename().wstring();
            if (name.empty()) name = paths[(size_t)i];
            drawText(dc, name, tr, v099Text(), uiFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        if ((int)paths.size() > count) {
            RECT more{ body.left + 30, body.top + count * rowH, body.right, body.bottom };
            drawText(dc, std::format(L"还有 {} 个项目", paths.size() - (size_t)count), more,
                v099Muted(), smallFont, DT_LEFT | DT_TOP | DT_SINGLELINE);
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

        RECT card = rc;
        card.left += 8; card.right -= 8;
        card.top += 6; card.bottom -= 6;

        const COLORREF cardBg = (active || selected) ? v099Selected() : (hovered ? v099Hover() : v099Card());
        fillRoundRect(dis->hDC, card, cardBg, 10);
        strokeRoundRect(dis->hDC, card, (active || selected) ? v099Primary() : v099Border(),
            (active || selected) ? 2 : 1, 10);

        RECT typeRc{ card.left + 14, card.top + 12, card.left + 64, card.top + 36 };
        fillRoundRect(dis->hDC, typeRc, selected ? v099PrimarySoft() : v099Surface(), 8);
        const wchar_t* typeName = item.type == ItemType::Image ? L"图像" :
            (item.type == ItemType::File ? L"文件" : L"文本");
        drawText(dis->hDC, typeName, typeRc, selected ? v099Primary() : v099Muted(), smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT timeRc{ card.right - 124, card.top + 10, card.right - 16, card.top + 36 };
        drawText(dis->hDC, relativeTime(item.updated), timeRc, v099Faint(), smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (item.favorite) {
            RECT fav{ card.right - 152, card.top + 10, card.right - 126, card.top + 36 };
            drawText(dis->hDC, L"★", fav, v099Primary(), uiFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if (selected) {
            RECT check{ card.left + 70, card.top + 13, card.left + 92, card.top + 35 };
            fillRoundRect(dis->hDC, check, v099Primary(), 11);
            drawText(dis->hDC, L"✓", check, RGB(255,255,255), smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        bool oversized = false;
        if (item.type == ItemType::Image) {
            RECT preview{ card.left + 14, card.top + 44, card.left + 276, card.bottom - 34 };
            fillRoundRect(dis->hDC, preview, v099Surface(), 8);
            strokeRoundRect(dis->hDC, preview, v099Border(), 1, 8);
            RECT imageRc = preview; InflateRect(&imageRc, -8, -8);
            drawDibFit(dis->hDC, item, imageRc);

            RECT imageTitle{ preview.right + 18, card.top + 52, card.right - 18, card.top + 78 };
            drawText(dis->hDC, L"图片", imageTitle, v099Text(), boldFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT imageInfo{ preview.right + 18, card.top + 80, card.right - 18, card.top + 108 };
            drawText(dis->hDC, imageMeta(item), imageInfo, v099Muted(), smallFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT hint{ preview.right + 18, card.top + 108, card.right - 18, card.top + 136 };
            drawText(dis->hDC, L"双击即可粘贴", hint, v099Faint(), smallFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else if (item.type == ItemType::File) {
            RECT body{ card.left + 14, card.top + 44, card.right - 18, card.bottom - 32 };
            v099DrawFilePreview(dis->hDC, item, body);
        }
        else {
            auto previewText = itemPreview(item, oversized);
            RECT body{ card.left + 14, card.top + 45, card.right - 18, card.bottom - 31 };
            drawText(dis->hDC, previewText, body, v099Text(), uiFont,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        std::wstring meta;
        if (item.type == ItemType::Image) meta = imageMeta(item);
        else if (item.type == ItemType::File) meta = std::format(L"{} 个项目", filePaths(item.data).size());
        else meta = std::format(L"{} 字符", textFromData(item.data).size());

        RECT metaRc{ card.right - 180, card.bottom - 28, card.right - 16, card.bottom - 7 };
        drawText(dis->hDC, meta, metaRc, v099Faint(), smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const bool expandable = item.type == ItemType::Image || item.type == ItemType::File || oversized;
        if (expandable) {
            RECT er = expandRect(rc);
            drawText(dis->hDC, L"查看", er, v099Muted(), smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void v099Layout(HWND hwnd)
    {
        if (!hwnd) return;
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = std::max(1, (int)rc.right);
        const int h = std::max(1, (int)rc.bottom);

        if (listWnd)
            MoveWindow(listWnd, 10, V099_HEADER_H, std::max(1, w - 20), std::max(1, h - V099_HEADER_H - 10), TRUE);

        if (searchWnd) {
            RECT outer = v099SearchOuterRect(rc);
            if (!multiMode) {
                MoveWindow(searchWnd, outer.left + 30, outer.top + 5,
                    std::max(80, outer.right - outer.left - 40), outer.bottom - outer.top - 10, TRUE);
                ShowWindow(searchWnd, SW_SHOW);
            }
            else {
                ShowWindow(searchWnd, SW_HIDE);
            }
        }

        if (clearWnd) ShowWindow(clearWnd, SW_HIDE);
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
    {
        if (msg == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE) {
                if (GetWindowTextLengthW(hwnd) > 0) {
                    SetWindowTextW(hwnd, L"");
                    refreshList();
                    return 0;
                }
                if (listWnd) SetFocus(listWnd);
                return 0;
            }
            if (wParam == VK_RETURN) {
                useListItem(currentListIndex(), true);
                return 0;
            }
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
        LRESULT result = ClipboardHistoryLegacy::listProc(hwnd, msg, wParam, lParam);
        if (msg == WM_KEYDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
            msg == WM_RBUTTONUP || msg == WM_CHAR) {
            if (historyWnd) {
                v099Layout(historyWnd);
                InvalidateRect(historyWnd, nullptr, FALSE);
            }
        }
        return result;
    }

    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd;

            // Clipboard UI is intentionally light-only in this rebuild.
            themeMode = ThemeMode::Light;

            v099EnsureFonts(hwnd);
            v099SyncBrushes();

            listWnd = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_LIST), GetModuleHandleW(nullptr), nullptr);

            searchWnd = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_SEARCH), GetModuleHandleW(nullptr), nullptr);

            clearWnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
                0,0,0,0, hwnd, reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR_FLOAT), GetModuleHandleW(nullptr), nullptr);

            if (listWnd) {
                SendMessageW(listWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldListProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)v099ListProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                oldSearchProc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(searchWnd, GWLP_WNDPROC, (LONG_PTR)v099SearchProc));
                SendMessageW(searchWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
            }

            refreshList();
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

            if (pointIn(v099CloseRect(rc), p) || pointIn(v099ClearRect(rc), p) ||
                pointIn(v099PinRect(rc), p) || pointIn(v099MultiRect(rc), p))
                return HTCLIENT;

            for (int i = 0; i < 5; ++i)
                if (pointIn(v099TabRect(rc, i), p)) return HTCLIENT;

            if (p.y < V099_TOP_H) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_SIZE:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            const int width = (int)rc.right;
            const bool widthChanged = std::abs(width - v099LastWidth) > 80;
            v099LastWidth = width;
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
                    mi->itemHeight = 100;
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
            if (pointIn(v099ClearRect(rc), p)) {
                clearAll();
                return 0;
            }
            if (pointIn(v099PinRect(rc), p)) {
                v099SetPinned(hwnd);
                return 0;
            }
            if (pointIn(v099MultiRect(rc), p)) {
                multiMode = !multiMode;
                if (!multiMode) {
                    multiHashes.clear();
                    multiAnchor = -1;
                }
                v099Layout(hwnd);
                if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                return 0;
            }

            for (int i = 0; i < 5; ++i) {
                if (pointIn(v099TabRect(rc, i), p)) {
                    setTab((Tab)i);
                    v099Layout(hwnd);
                    return 0;
                }
            }

            if (multiMode) {
                RECT bar = v099SearchOuterRect(rc);
                RECT copy{ bar.right - 190, bar.top + 3, bar.right - 132, bar.bottom - 3 };
                RECT paste{ bar.right - 126, bar.top + 3, bar.right - 68, bar.bottom - 3 };
                RECT exit{ bar.right - 62, bar.top + 3, bar.right - 6, bar.bottom - 3 };

                if (pointIn(copy, p)) {
                    copyMulti(false);
                    return 0;
                }
                if (pointIn(paste, p)) {
                    copyMulti(true);
                    return 0;
                }
                if (pointIn(exit, p)) {
                    multiMode = false;
                    multiHashes.clear();
                    multiAnchor = -1;
                    v099Layout(hwnd);
                    if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                    return 0;
                }
            }
            break;
        }

        case WM_SETTINGCHANGE:
            // Deliberately ignore OS dark-mode changes for this light-only clipboard window.
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
            oldListProc = nullptr;
            oldSearchProc = nullptr;
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            v099DrawHeader(dc, rc);
            RECT frame{ 0, 0, std::max(1, (int)rc.right - 1), std::max(1, (int)rc.bottom - 1) };
            strokeRoundRect(dc, frame, v099Border(), 1, 8);
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
            const int ww = std::min(980, std::max(760, workW - 180));
            const int wh = std::min(760, std::max(560, workH - 160));
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
        ClipboardHistoryLegacy::dispose();
    }
}
