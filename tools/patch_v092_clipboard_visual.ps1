$ErrorActionPreference = 'Stop'

$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw

$src = $src.Replace('// WeShot clipboard manager v0.9.1', '// WeShot clipboard manager v0.9.2')

if (-not $src.Contains('inline int hoverAction{ -1 };')) {
    $src = $src.Replace(
        '    inline std::unordered_set<uint64_t> multiHashes;',
        "    inline std::unordered_set<uint64_t> multiHashes;`r`n    inline int hoverAction{ -1 };`r`n    inline double fullImageZoom{ 1.0 };"
    )
}

$themePattern = '(?s)    inline Theme theme\(\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void fillRect'
$themeReplacement = @'
    inline Theme theme()
    {
        // Stronger uTools-like blue accent, while keeping native Windows light/dark support.
        if (isDarkMode()) {
            return { RGB(0x5b,0x7c,0xff), RGB(0x27,0x31,0x48), RGB(0xf3,0xf5,0xf7),
                RGB(0x9d,0xa4,0xb2), RGB(0x2b,0x30,0x3a), RGB(0x35,0x3b,0x48),
                RGB(0x20,0x24,0x2c), RGB(0x2a,0x31,0x43), RGB(0x18,0x1c,0x23) };
        }
        return { RGB(0x42,0x63,0xd8), RGB(0xeb,0xf0,0xff), RGB(0x24,0x29,0x33),
            RGB(0x78,0x82,0x92), RGB(0xf4,0xf6,0xfa), RGB(0xe3,0xe8,0xf1),
            RGB(0xf7,0xf8,0xfb), RGB(0xe9,0xee,0xff), RGB(0xff,0xff,0xff) };
    }

    inline void fillRect
'@
$patched = [regex]::Replace($src, $themePattern, $themeReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 theme replacement failed' }
$src = $patched

if (-not $src.Contains('inline void strokeRoundRect')) {
    $needle = @'
    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
'@
    $insert = @'
    inline void strokeRoundRect(HDC dc, const RECT& rc, COLORREF color, int width = 1, int radius = 8)
    {
        HPEN p = CreatePen(PS_SOLID, width, color);
        auto oldP = SelectObject(dc, p);
        auto oldB = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(p);
    }

    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
'@
    if (-not $src.Contains($needle)) { throw 'v0.9.2 stroke insertion point missing' }
    $src = $src.Replace($needle, $insert)
}

$dibPattern = '(?s)    inline void drawDibFit\(HDC dc, const Item& item, RECT rc\).*?\r?\n    \}\r?\n\r?\n    inline std::wstring itemPreview'
$dibReplacement = @'
    inline std::wstring readableBytes(size_t bytes)
    {
        if (bytes >= 1024ull * 1024ull)
            return std::format(L"{:.1f} MB", (double)bytes / (1024.0 * 1024.0));
        if (bytes >= 1024ull)
            return std::format(L"{:.0f} KB", (double)bytes / 1024.0);
        return std::format(L"{} B", bytes);
    }

    inline std::wstring imageMeta(const Item& item)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return readableBytes(item.data.size());
        auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        return std::format(L"{} × {}   ·   {}", std::abs(bi->biWidth), std::abs(bi->biHeight), readableBytes(item.data.size()));
    }

    inline void drawDibZoom(HDC dc, const Item& item, RECT rc, double zoom)
    {
        if (item.data.size() < sizeof(BITMAPINFOHEADER)) return;
        auto bi = reinterpret_cast<const BITMAPINFOHEADER*>(item.data.data());
        if (bi->biWidth == 0 || bi->biHeight == 0) return;
        size_t off = dibBitsOffset(bi);
        if (off >= item.data.size()) return;
        int sw = std::abs(bi->biWidth), sh = std::abs(bi->biHeight);
        int aw = std::max(1, (int)(rc.right - rc.left)), ah = std::max(1, (int)(rc.bottom - rc.top));
        double fit = std::min((double)aw / sw, (double)ah / sh);
        double scale = fit * std::clamp(zoom, 1.0, 4.0);
        int dw = std::max(1, (int)std::lround(sw * scale));
        int dh = std::max(1, (int)std::lround(sh * scale));
        int x = rc.left + (aw - dw) / 2, y = rc.top + (ah - dh) / 2;
        int saved = SaveDC(dc);
        IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, dw, dh, 0, 0, sw, sh,
            item.data.data() + off, reinterpret_cast<const BITMAPINFO*>(bi), DIB_RGB_COLORS, SRCCOPY);
        RestoreDC(dc, saved);
    }

    inline void drawDibFit(HDC dc, const Item& item, RECT rc)
    {
        drawDibZoom(dc, item, rc, 1.0);
    }

    inline std::wstring itemPreview
'@
$patched = [regex]::Replace($src, $dibPattern, $dibReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 image renderer replacement failed' }
$src = $patched

$actionPattern = '(?s)    inline void drawActionButton\(HDC dc, RECT rc, const std::wstring& glyph, const Theme& t\).*?\r?\n    \}\r?\n\r?\n    inline void drawListItem\(const DRAWITEMSTRUCT\* dis\).*?\r?\n    \}\r?\n\r?\n    inline void setTab'
$actionReplacement = @'
    inline void drawActionButton(HDC dc, RECT rc, const std::wstring& glyph, const Theme& t, bool hovered)
    {
        fillRoundRect(dc, rc, hovered ? t.primary : t.textBg, 9);
        if (!hovered) strokeRoundRect(dc, rc, t.textBgLighter, 1, 9);
        drawText(dc, glyph, rc, hovered ? t.bg : t.primary, emojiFont ? emojiFont : uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void drawHoverHint(HDC dc, const RECT& anchor, const wchar_t* text, const Theme& t)
    {
        int width = 72;
        RECT tip{ anchor.left - (width - (anchor.right - anchor.left)) / 2,
            anchor.top - 28, anchor.left - (width - (anchor.right - anchor.left)) / 2 + width, anchor.top - 5 };
        fillRoundRect(dc, tip, t.text, 7);
        drawText(dc, text, tip, t.bg, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    inline void drawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;
        const Theme t = theme();
        const Item& item = items[visibleItems[dis->itemID]];
        RECT rc = dis->rcItem;
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool multiSelected = multiHashes.contains(item.hash);
        fillRect(dis->hDC, rc, t.bg);

        RECT card = rc; InflateRect(&card, -5, -4);
        fillRoundRect(dis->hDC, card, (active || multiSelected) ? t.primaryLighter : t.bg, 10);
        if (active) strokeRoundRect(dis->hDC, card, t.primary, 2, 10);
        else strokeRoundRect(dis->hDC, card, t.textBgLighter, 1, 10);

        const int left = card.left + 8;
        RECT timeArea{ left, card.top + 7, left + 98, card.bottom - 7 };
        RECT badge{ timeArea.left + 7, timeArea.top + 7, timeArea.right - 7, timeArea.top + 35 };
        fillRoundRect(dis->hDC, badge, active ? t.bg : t.textBg, 8);
        drawText(dis->hDC, relativeTime(item.updated), badge, active ? t.primary : t.text, smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const wchar_t* typeName = item.type == ItemType::Image ? L"图片" : (item.type == ItemType::File ? L"文件" : L"文字");
        RECT typeBadge{ timeArea.left + 20, badge.bottom + 7, timeArea.right - 20, badge.bottom + 30 };
        fillRoundRect(dis->hDC, typeBadge, active ? t.primary : t.navHover, 7);
        drawText(dis->hDC, typeName, typeBadge, active ? t.bg : t.primary, smallFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT divider{ timeArea.right + 3, card.top + 9, timeArea.right + 4, card.bottom - 9 };
        fillRect(dis->hDC, divider, active ? t.primary : t.textBgLighter);

        const int actionReserve = active && !multiMode ? 190 : 62;
        RECT content{ timeArea.right + 16, card.top + 9, card.right - actionReserve, card.bottom - 9 };
        if (item.type == ItemType::Image) {
            RECT imagePanel = content;
            imagePanel.right = std::min(imagePanel.right, imagePanel.left + 560);
            imagePanel.bottom -= 27;
            fillRoundRect(dis->hDC, imagePanel, t.bg, 8);
            strokeRoundRect(dis->hDC, imagePanel, active ? t.primary : t.textBgLighter, 1, 8);
            RECT imageRc = imagePanel; InflateRect(&imageRc, -8, -8);
            drawDibFit(dis->hDC, item, imageRc);
            RECT meta{ imagePanel.left + 4, imagePanel.bottom + 3, imagePanel.right - 4, content.bottom };
            drawText(dis->hDC, imageMeta(item), meta, t.textLighter, smallFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        else {
            bool oversized = false;
            auto preview = itemPreview(item, oversized);
            drawText(dis->hDC, preview, content, t.text, uiFont,
                DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        if (active && !multiMode) {
            RECT acts[4]{}; actionRects(dis->hwndItem, card, acts);
            const wchar_t* icons[4] = { L"📋", L"🔍", item.favorite ? L"★" : L"☆", L"🗑" };
            const wchar_t* tips[4] = { L"复制", L"放大查看", item.favorite ? L"取消收藏" : L"收藏", L"删除" };
            for (int i = 0; i < 4; ++i) drawActionButton(dis->hDC, acts[i], icons[i], t, hoverAction == i);
            if (hoverAction >= 0 && hoverAction < 4) drawHoverHint(dis->hDC, acts[hoverAction], tips[hoverAction], t);
        }
        else {
            RECT count{ card.right - 55, card.top, card.right - 8, card.bottom };
            std::wstring s = std::to_wstring(dis->itemID + 1);
            if (multiSelected) s = L"✓ " + s;
            drawText(dis->hDC, s, count, multiSelected ? t.primary : t.textLighter,
                smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void setTab
'@
$patched = [regex]::Replace($src, $actionPattern, $actionReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 list item replacement failed' }
$src = $patched

$topPattern = '(?s)    inline void drawTopBar\(HDC dc, RECT client\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void ensureFonts'
$topReplacement = @'
    inline void drawTopBar(HDC dc, RECT client)
    {
        const Theme t = theme();
        RECT nav{ 0,0,client.right,66 };
        fillRect(dc, nav, t.navBg);
        RECT bottomLine{ 0,65,client.right,66 };
        fillRect(dc, bottomLine, t.textBgLighter);
        const wchar_t* labels[5] = { L"☰  全部", L"▤  文字", L"▧  图片", L"▱  文件", L"☆  收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = tabRect(i);
            bool active = (int)activeTab == i;
            if (active) {
                fillRoundRect(dc, r, t.bg, 9);
                strokeRoundRect(dc, r, t.textBgLighter, 1, 9);
                RECT accent{ r.left + 14, r.bottom - 4, r.right - 14, r.bottom - 1 };
                fillRoundRect(dc, accent, t.primary, 3);
            }
            drawText(dc, labels[i], r, active ? t.primary : t.text, uiFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if (multiMode) {
            RECT count{ client.right - 330, 13, client.right - 275, 53 };
            fillRoundRect(dc, count, t.primary, 8);
            drawText(dc, std::to_wstring(multiHashes.size()), count, t.bg, boldFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ client.right - 268, 13, client.right - 198, 53 };
            RECT paste{ client.right - 190, 13, client.right - 120, 53 };
            RECT exit{ client.right - 112, 13, client.right - 18, 53 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 8);
            drawText(dc, L"复制", copy, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出多选", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void ensureFonts
'@
$patched = [regex]::Replace($src, $topPattern, $topReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 topbar replacement failed' }
$src = $patched

$showPattern = '(?s)    inline void showFull\(int idx\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void hideFull'
$showReplacement = @'
    inline void showFull(int idx)
    {
        Item* item = itemAtListIndex(idx);
        if (!item || !historyWnd) return;
        fullItemHash = item->hash;
        fullImageZoom = 1.0;
        if (!fullWnd) {
            fullWnd = CreateWindowExW(0, L"WeShotClipboardFullView", L"", WS_CHILD,
                0,0,0,0, historyWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        }
        if (!fullWnd) return;
        RECT rc{}; GetClientRect(historyWnd, &rc);
        MoveWindow(fullWnd, 0, 0, rc.right, rc.bottom, TRUE);
        ShowWindow(fullWnd, SW_SHOW);
        BringWindowToTop(fullWnd);
        SetFocus(fullWnd);
        InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void hideFull
'@
$patched = [regex]::Replace($src, $showPattern, $showReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 showFull replacement failed' }
$src = $patched

$listPattern = '(?s)    inline LRESULT CALLBACK listProc\(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline LRESULT CALLBACK fullProc'
$listReplacement = @'
    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_MOUSEMOVE:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                if ((int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0) != idx) {
                    SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                    hoverAction = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                RECT itemRc{}; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
                RECT card = itemRc; InflateRect(&card, -5, -4);
                RECT acts[4]{}; actionRects(hwnd, card, acts);
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                int nextHover = -1;
                if (!multiMode) for (int i = 0; i < 4; ++i) if (pointIn(acts[i], p)) { nextHover = i; break; }
                if (nextHover != hoverAction) { hoverAction = nextHover; InvalidateRect(hwnd, &itemRc, FALSE); }
            }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            break;
        }
        case WM_MOUSELEAVE:
            if (hoverAction != -1) { hoverAction = -1; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (HIWORD(hit) || idx < 0 || idx >= (int)visibleItems.size()) break;
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            if (multiMode) { toggleMultiCurrent(); return 0; }
            RECT itemRc{}; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
            RECT card = itemRc; InflateRect(&card, -5, -4);
            RECT acts[4]{}; actionRects(hwnd, card, acts);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pointIn(acts[0], p)) { useListItem(idx, false); return 0; }
            if (pointIn(acts[1], p)) { showFull(idx); return 0; }
            if (pointIn(acts[2], p)) { toggleFavorite(idx); return 0; }
            if (pointIn(acts[3], p)) { deleteListItem(idx); return 0; }
            useListItem(idx, true); return 0;
        }
        case WM_RBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit)) useListItem(idx, false);
            return 0;
        }
        case WM_KEYDOWN:
        {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (wParam == VK_ESCAPE) { if (historyWnd) ShowWindow(historyWnd, SW_HIDE); return 0; }
            if (wParam == VK_RETURN) { useListItem(currentListIndex(), true); return 0; }
            if (wParam == VK_DELETE) { deleteListItem(currentListIndex()); return 0; }
            if (wParam == VK_SPACE) { toggleMultiCurrent(); return 0; }
            if (wParam == VK_TAB) { setTab((Tab)(((int)activeTab + 1) % 5)); return 0; }
            if (ctrl && (wParam == 'F' || wParam == 'L')) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && wParam == 'C') { useListItem(currentListIndex(), false); return 0; }
            if (ctrl && (wParam == 'J' || wParam == 'K')) {
                int idx = currentListIndex();
                idx += wParam == 'J' ? 1 : -1;
                idx = std::clamp(idx, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, idx - 3), 0);
                return 0;
            }
            if ((ctrl || alt) && wParam >= '1' && wParam <= '9') {
                int idx = (int)(wParam - '1');
                if (idx < (int)visibleItems.size()) useListItem(idx, true);
                return 0;
            }
            if (wParam == VK_SHIFT) {
                multiMode = true;
                if (searchWnd) ShowWindow(searchWnd, SW_HIDE);
                if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
                return 0;
            }
            break;
        }
        }
        return oldListProc ? CallWindowProcW(oldListProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK fullProc
'@
$patched = [regex]::Replace($src, $listPattern, $listReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 listProc replacement failed' }
$src = $patched

$fullPattern = '(?s)    inline LRESULT CALLBACK fullProc\(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void clearAll'
$fullReplacement = @'
    inline LRESULT CALLBACK fullProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEWHEEL:
        {
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                fullImageZoom = std::clamp(fullImageZoom + steps * 0.25, 1.0, 4.0);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { hideFull(); return 0; }
            if (wParam == VK_RETURN) { restoreByHash(fullItemHash, true); return 0; }
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 4.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 4.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == '0') { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            break;
        case WM_LBUTTONDOWN:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            int panelW = (int)(rc.right * .86);
            RECT panel{ 0,0,panelW,rc.bottom };
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!pointIn(panel, p)) { hideFull(); return 0; }
            RECT zoomOut{ panelW - 390,15,panelW - 350,55 };
            RECT zoomReset{ panelW - 343,15,panelW - 273,55 };
            RECT zoomIn{ panelW - 266,15,panelW - 226,55 };
            RECT copy{ panelW - 215,15,panelW - 170,55 };
            RECT fav{ panelW - 160,15,panelW - 115,55 };
            RECT del{ panelW - 105,15,panelW - 60,55 };
            RECT close{ panelW - 50,15,panelW - 10,55 };
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                if (pointIn(zoomOut, p)) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 4.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomReset, p)) { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomIn, p)) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 4.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            }
            if (pointIn(copy, p)) { restoreByHash(fullItemHash, false); return 0; }
            if (pointIn(fav, p)) {
                if (item) { item->favorite = !item->favorite; saveStore(); refreshList(); }
                InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (pointIn(del, p)) {
                for (size_t i = 0; i < items.size(); ++i) if (items[i].hash == fullItemHash) {
                    items.erase(items.begin() + (ptrdiff_t)i); break;
                }
                saveStore(); refreshList(); hideFull(); return 0;
            }
            if (pointIn(close, p)) { hideFull(); return 0; }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            Theme t = theme();
            fillRect(dc, rc, RGB(0x31,0x35,0x3d));
            int panelW = (int)(rc.right * .86);
            RECT panel{ 0,0,panelW,rc.bottom };
            fillRect(dc, panel, t.bg);
            auto* item = findByHash(fullItemHash);
            if (item) {
                RECT title{ 22,12,panelW - 410,58 };
                std::wstring heading = item->type == ItemType::Image ? L"图片预览  ·  鼠标滚轮 / +/- 缩放" : L"完整内容";
                drawText(dc, heading, title, t.text, boldFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT zoomOut{ panelW - 390,15,panelW - 350,55 };
                RECT zoomReset{ panelW - 343,15,panelW - 273,55 };
                RECT zoomIn{ panelW - 266,15,panelW - 226,55 };
                RECT copy{ panelW - 215,15,panelW - 170,55 };
                RECT fav{ panelW - 160,15,panelW - 115,55 };
                RECT del{ panelW - 105,15,panelW - 60,55 };
                RECT close{ panelW - 50,15,panelW - 10,55 };
                if (item->type == ItemType::Image) {
                    for (auto r : { zoomOut,zoomReset,zoomIn }) fillRoundRect(dc, r, t.navHover, 8);
                    drawText(dc, L"−", zoomOut, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    std::wstring zoomLabel = fullImageZoom <= 1.001 ? L"适应" : std::format(L"{}%", (int)std::lround(fullImageZoom * 100.0));
                    drawText(dc, zoomLabel, zoomReset, t.primary, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    drawText(dc, L"+", zoomIn, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                for (auto r : { copy,fav,del,close }) fillRoundRect(dc, r, t.textBg, 8);
                drawText(dc, L"📋", copy, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, item->favorite ? L"★" : L"☆", fav, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"🗑", del, t.primary, emojiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"×", close, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                RECT content{ 20,70,panelW - 20,rc.bottom - 20 };
                fillRoundRect(dc, content, t.textBg, 10);
                strokeRoundRect(dc, content, t.textBgLighter, 1, 10);
                RECT inner{ content.left + 18, content.top + 18, content.right - 18, content.bottom - 18 };
                if (item->type == ItemType::Image) drawDibZoom(dc, *item, inner, fullImageZoom);
                else {
                    std::wstring s = item->type == ItemType::Text ? textFromData(item->data) : filePreview(item->data, 1000);
                    drawText(dc, s, inner, t.text, uiFont, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }
            }
            EndPaint(hwnd, &ps); return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline void clearAll
'@
$patched = [regex]::Replace($src, $fullPattern, $fullReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.2 full preview replacement failed' }
$src = $patched

$src = $src.Replace('mi->itemHeight = 128;', 'mi->itemHeight = 198;')

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @(
    'fullImageZoom',
    '放大查看',
    'imageMeta(item)',
    'HALFTONE',
    'mi->itemHeight = 198;',
    '鼠标滚轮 / +/- 缩放'
)) {
    if (-not $verify.Contains($needle)) { throw "v0.9.2 clipboard visual verification failed: $needle" }
}
Write-Host 'v0.9.2 clipboard visual polish applied: larger image previews, zoom, hover hints, stronger uTools-like blue theme.'
