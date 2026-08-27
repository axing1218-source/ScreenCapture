$ErrorActionPreference = 'Stop'

# WeShot v0.9.6
# - Use current uTools clipboard visual colors measured from the user's official
#   light/dark screenshots, while preserving the public ClipboardManager layout logic.
# - Make the right action rail match the supplied official crop more closely:
#   72px wide, 52px circles, 78px cadence, theme-aware colors.
# - Separate hover from selection. Current uTools single click selects; simply moving
#   the mouse must not silently change the active clipboard record.
# - Flatten the search field into the nav bar and keep "搜索..." deterministic.

$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw
$src = $src.Replace('// WeShot clipboard manager v0.9.5', '// WeShot clipboard manager v0.9.6')

if (-not $src.Contains('inline int hoverIndex{ -1 };')) {
    $src = $src.Replace(
        '    inline int multiAnchor{ -1 };',
        "    inline int multiAnchor{ -1 };`r`n    inline int hoverIndex{ -1 };"
    )
}

$themePattern = '(?s)    inline Theme theme\(\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void fillRect'
$themeReplacement = @'
    inline Theme theme()
    {
        if (useDarkTheme()) {
            return { RGB(0x90,0xca,0xf9), RGB(0x58,0x70,0xff), RGB(0xf8,0xf8,0xf8),
                RGB(0xb5,0xb5,0xb5), RGB(0x50,0x50,0x50), RGB(0x4a,0x4a,0x4a),
                RGB(0x30,0x31,0x33), RGB(0x4a,0x4a,0x4a), RGB(0x21,0x21,0x21) };
        }
        return { RGB(0x58,0x70,0xff), RGB(0x90,0xca,0xf9), RGB(0x21,0x21,0x21),
            RGB(0x70,0x70,0x70), RGB(0xf4,0xf4,0xf4), RGB(0xd0,0xd0,0xd0),
            RGB(0xf4,0xf4,0xf4), RGB(0xdb,0xdb,0xdb), RGB(0xff,0xff,0xff) };
    }

    inline COLORREF selectionBg()
    {
        return useDarkTheme() ? RGB(0x26,0x2f,0x37) : RGB(0xe3,0xf2,0xfd);
    }

    inline void fillRect
'@
$patched = [regex]::Replace($src, $themePattern, $themeReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.6 theme replacement failed' }
$src = $patched

$railPattern = '(?s)    inline int sideRailWidth\(\).*?\r?\n\r?\n    inline RECT tabRect\(int index\)'
$railReplacement = @'
    inline int sideRailWidth() { return 72; }

    inline RECT sideRailButtonRect(const RECT& client, int index)
    {
        const int railLeft = client.right - sideRailWidth();
        const int cx = railLeft + sideRailWidth() / 2;
        if (index < 3) {
            const int cy = 41 + index * 78;
            return { cx - 26, cy - 26, cx + 26, cy + 26 };
        }
        const int cy = client.bottom - 173 + (index - 3) * 78;
        return { cx - 24, cy - 24, cx + 24, cy + 24 };
    }

    inline void drawRailGlyph(HDC dc, int index, const RECT& r, COLORREF color)
    {
        HPEN pen = CreatePen(PS_SOLID, 2, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int cx = (r.left + r.right) / 2;
        const int cy = (r.top + r.bottom) / 2;

        if (index == 0) {
            HPEN dot = CreatePen(PS_DOT, 1, color);
            SelectObject(dc, dot);
            Rectangle(dc, cx - 13, cy - 12, cx + 13, cy + 12);
            SelectObject(dc, pen);
            RoundRect(dc, cx - 6, cy - 8, cx + 6, cy + 8, 3, 3);
            MoveToEx(dc, cx - 2, cy + 5, nullptr); LineTo(dc, cx + 2, cy + 5);
            DeleteObject(dot);
        }
        else if (index == 1) {
            POINT p[3] = { {cx - 10, cy - 10}, {cx + 12, cy}, {cx - 10, cy + 10} };
            HBRUSH b = CreateSolidBrush(color);
            SelectObject(dc, b); Polygon(dc, p, 3);
            SelectObject(dc, oldBrush); DeleteObject(b);
        }
        else if (index == 2) {
            Rectangle(dc, cx - 6, cy - 10, cx + 6, cy - 4);
            MoveToEx(dc, cx - 4, cy - 4, nullptr); LineTo(dc, cx - 4, cy + 3);
            MoveToEx(dc, cx + 4, cy - 4, nullptr); LineTo(dc, cx + 4, cy + 3);
            MoveToEx(dc, cx - 9, cy + 3, nullptr); LineTo(dc, cx + 9, cy + 3);
            MoveToEx(dc, cx, cy + 3, nullptr); LineTo(dc, cx, cy + 12);
        }
        else if (index == 3) {
            Rectangle(dc, cx - 10, cy - 8, cx + 7, cy + 9);
            MoveToEx(dc, cx - 1, cy + 1, nullptr); LineTo(dc, cx + 11, cy - 11);
            MoveToEx(dc, cx + 4, cy - 11, nullptr); LineTo(dc, cx + 11, cy - 11);
            LineTo(dc, cx + 11, cy - 4);
        }
        else if (index == 4) {
            Ellipse(dc, cx - 7, cy - 7, cx + 7, cy + 7);
            Ellipse(dc, cx - 2, cy - 2, cx + 2, cy + 2);
            const POINT a[8] = {
                {cx,cy-12},{cx+8,cy-8},{cx+12,cy},{cx+8,cy+8},
                {cx,cy+12},{cx-8,cy+8},{cx-12,cy},{cx-8,cy-8}
            };
            const POINT b[8] = {
                {cx,cy-8},{cx+6,cy-6},{cx+8,cy},{cx+6,cy+6},
                {cx,cy+8},{cx-6,cy+6},{cx-8,cy},{cx-6,cy-6}
            };
            for (int i = 0; i < 8; ++i) {
                MoveToEx(dc, b[i].x, b[i].y, nullptr); LineTo(dc, a[i].x, a[i].y);
            }
        }
        else if (index == 5) {
            MoveToEx(dc, cx + 7, cy - 12, nullptr); LineTo(dc, cx - 3, cy + 3);
            MoveToEx(dc, cx - 8, cy + 2, nullptr); LineTo(dc, cx + 2, cy + 9);
            MoveToEx(dc, cx - 10, cy + 5, nullptr); LineTo(dc, cx, cy + 12);
            MoveToEx(dc, cx - 4, cy + 1, nullptr); LineTo(dc, cx + 5, cy + 8);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    inline void drawSideRail(HDC dc, const RECT& client)
    {
        const bool dark = useDarkTheme();
        const int railLeft = client.right - sideRailWidth();
        RECT rail{ railLeft, 0, client.right, client.bottom };
        fillRect(dc, rail, dark ? RGB(0x30,0x31,0x33) : RGB(0xf4,0xf4,0xf4));

        const COLORREF circle = dark ? RGB(0x90,0xca,0xf9) : RGB(0x58,0x70,0xff);
        const COLORREF circleGlyph = dark ? RGB(0x16,0x2b,0x3a) : RGB(0xff,0xff,0xff);
        const COLORREF lowerGlyph = dark ? RGB(0xf2,0xf2,0xf2) : RGB(0x70,0x70,0x70);

        for (int i = 0; i < 3; ++i) {
            RECT r = sideRailButtonRect(client, i);
            HBRUSH b = CreateSolidBrush(circle);
            HGDIOBJ oldB = SelectObject(dc, b);
            HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldP); SelectObject(dc, oldB); DeleteObject(b);
            drawRailGlyph(dc, i, r, circleGlyph);
        }
        for (int i = 3; i < 6; ++i) {
            drawRailGlyph(dc, i, sideRailButtonRect(client, i), lowerGlyph);
        }
    }

    inline RECT tabRect(int index)
'@
$patched = [regex]::Replace($src, $railPattern, $railReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.6 rail replacement failed' }
$src = $patched

$tabPattern = '(?s)    inline RECT tabRect\(int index\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline RECT themeButtonRect'
$tabReplacement = @'
    inline RECT tabRect(int index)
    {
        RECT cr{};
        if (historyWnd) GetClientRect(historyWnd, &cr);
        int w = std::max(520, (int)(cr.right - cr.left) - sideRailWidth());
        int tabW = std::clamp((w - 10) / 5, 92, 170);
        int x = 5 + index * tabW;
        return { x, 58, x + tabW, 106 };
    }

    inline RECT themeButtonRect
'@
$patched = [regex]::Replace($src, $tabPattern, $tabReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.6 tab geometry replacement failed' }
$src = $patched

$topPattern = '(?s)    inline void drawTopBar\(HDC dc, RECT client\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void ensureFonts'
$topReplacement = @'
    inline void drawTopBar(HDC dc, RECT client)
    {
        const Theme t = theme();
        RECT top{ 0,0,client.right,58 };
        fillRect(dc, top, t.navBg);
        RECT tabs{ 0,58,client.right,106 };
        fillRect(dc, tabs, t.navBg);

        RECT themeRc = themeButtonRect(client);
        fillRoundRect(dc, themeRc, t.navHover, 6);
        std::wstring themeText = std::format(L"◐  {}", themeModeLabel());
        drawText(dc, themeText, themeRc, t.primary, uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = { L"▣  全部", L"T  文本", L"▧  图像", L"▱  文件",
            favCount ? std::format(L"★  收藏 ({})", favCount) : L"★  收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = tabRect(i);
            bool selected = (int)activeTab == i;
            if (selected) {
                fillRoundRect(dc, r, t.bg, 7);
                RECT squareBottom{ r.left, r.bottom - 8, r.right, r.bottom };
                fillRect(dc, squareBottom, t.bg);
            }
            drawText(dc, labels[i], r, selected ? t.text : t.textLighter,
                selected ? boldFont : uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT separator{ 0,105,client.right,106 };
        fillRect(dc, separator, t.textBgLighter);

        if (multiMode) {
            RECT multiBg{ 16,11,std::max(130, (int)client.right - 146),50 };
            fillRoundRect(dc, multiBg, t.bg, 5);
            RECT count{ multiBg.left + 10,15,multiBg.left + 100,47 };
            drawText(dc, std::format(L"已选 {}", multiHashes.size()), count, t.primary, boldFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ multiBg.right - 220,14,multiBg.right - 154,48 };
            RECT paste{ multiBg.right - 148,14,multiBg.right - 82,48 };
            RECT exit{ multiBg.right - 76,14,multiBg.right - 10,48 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 5);
            drawText(dc, L"复制", copy, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void ensureFonts
'@
$patched = [regex]::Replace($src, $topPattern, $topReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.6 top bar replacement failed' }
$src = $patched

$src = $src.Replace('editBrush = CreateSolidBrush(t.textBg);', 'editBrush = CreateSolidBrush(t.navBg);')
$src = $src.Replace('SetTextColor(dc, t.text); SetBkColor(dc, t.textBg);', 'SetTextColor(dc, t.text); SetBkColor(dc, t.navBg);')
$src = $src.Replace('editBrush = CreateSolidBrush(theme().bg);', 'editBrush = CreateSolidBrush(theme().navBg);')

$drawPattern = '(?s)    inline void drawListItem\(const DRAWITEMSTRUCT\* dis\).*?\r?\n    \}\r?\n\r?\n    inline void setTab'
$drawReplacement = @'
    inline void drawListItem(const DRAWITEMSTRUCT* dis)
    {
        if (!dis || dis->itemID == (UINT)-1 || dis->itemID >= visibleItems.size()) return;
        const Theme t = theme();
        const Item& item = items[visibleItems[dis->itemID]];
        RECT rc = dis->rcItem;
        const bool active = (dis->itemState & ODS_SELECTED) != 0;
        const bool multiSelected = multiHashes.contains(item.hash);
        const bool hovered = ((int)dis->itemID == hoverIndex);

        fillRect(dis->hDC, rc, t.bg);
        RECT card = rc; InflateRect(&card, -2, 0);
        if (hovered || active || multiSelected) fillRect(dis->hDC, card, selectionBg());

        if (active || multiSelected) {
            strokeRoundRect(dis->hDC, card, t.primary, 2, 3);
        }
        else {
            RECT line{ card.left + 24, card.bottom - 1, card.right - 12, card.bottom };
            fillRect(dis->hDC, line, t.textBgLighter);
        }

        RECT body{ card.left + 36, card.top + 8, card.right - 30, card.bottom - 31 };
        bool oversized = false;
        if (item.type == ItemType::Image) {
            RECT imageRc = body;
            imageRc.left += 28; imageRc.right -= 28;
            drawDibFit(dis->hDC, item, imageRc);
        }
        else {
            auto preview = itemPreview(item, oversized);
            drawText(dis->hDC, preview, body, t.text, uiFont,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        RECT timeRc{ card.left + 36, card.bottom - 29, card.left + 180, card.bottom - 4 };
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

        RECT metaRc{ card.right - 250, card.bottom - 29, card.right - 60, card.bottom - 4 };
        drawText(dis->hDC, meta, metaRc, t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT indexRc{ card.right - 54, card.bottom - 29, card.right - 14, card.bottom - 4 };
        std::wstring index = std::to_wstring(dis->itemID + 1);
        if (multiSelected) index = L"✓ " + index;
        drawText(dis->hDC, index, indexRc, multiSelected ? t.primary : t.textLighter, smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        if (item.favorite) {
            RECT favRc{ card.right - 52, card.top + 7, card.right - 16, card.top + 32 };
            drawText(dis->hDC, L"★", favRc, t.primary, uiFont,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void setTab
'@
$patched = [regex]::Replace($src, $drawPattern, $drawReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.6 list rendering replacement failed' }
$src = $patched

$listSwitchNeedle = @'
        switch (msg) {
        case WM_LBUTTONUP:
'@
$listSwitchReplacement = @'
        switch (msg) {
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int nextHover = HIWORD(hit) ? -1 : LOWORD(hit);
            if (nextHover < 0 || nextHover >= (int)visibleItems.size()) nextHover = -1;
            if (nextHover != hoverIndex) {
                hoverIndex = nextHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (hoverIndex != -1) {
                hoverIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
'@
if (-not $src.Contains($listSwitchNeedle)) { throw 'v0.9.6 list hover insertion target missing' }
$src = $src.Replace($listSwitchNeedle, $listSwitchReplacement)

$oldLayoutSearch = @'
        if (searchWnd) {
            int x = 30;
            int right = std::max(x + 90, contentW - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
'@
$newLayoutSearch = @'
        if (searchWnd) {
            int x = 24;
            int right = std::max(x + 90, contentW - 148);
            MoveWindow(searchWnd, x, 12, std::max(90, right - x - 8), 34, TRUE);
        }
'@
if (-not $src.Contains($oldLayoutSearch)) { throw 'v0.9.6 search layout target missing' }
$src = $src.Replace($oldLayoutSearch, $newLayoutSearch)

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @(
    '// WeShot clipboard manager v0.9.6',
    'RGB(0x58,0x70,0xff)',
    'RGB(0x90,0xca,0xf9)',
    'sideRailWidth() { return 72; }',
    'const int cy = 41 + index * 78;',
    'inline int hoverIndex{ -1 };',
    'case WM_MOUSELEAVE:',
    'selectionBg()',
    'MoveWindow(searchWnd, x, 12'
)) {
    if (-not $verify.Contains($needle)) { throw "v0.9.6 verification failed: $needle" }
}

Write-Host 'v0.9.6 applied: current-uTools palette, theme-aware rail, hover/selection separation, flat search.'
