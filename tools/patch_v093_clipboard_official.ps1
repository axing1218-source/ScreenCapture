$ErrorActionPreference = 'Stop'

$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw

# v0.9.3 is based on the v0.9.2 generated source.  The goals of this pass are:
# - fix the broken child-window image preview by making it a real owned top-level window
# - follow the current official uTools clipboard interaction model (single click selects,
#   double click pastes, right click opens an action menu)
# - move the visual language closer to the current official wide/narrow screenshots
# - add an explicit System / Light / Dark clipboard theme switch

$src = $src.Replace('// WeShot clipboard manager v0.9.2', '// WeShot clipboard manager v0.9.3')

if (-not $src.Contains('enum class ThemeMode')) {
    $src = $src.Replace(
        '    enum class Tab : uint8_t { All = 0, Text = 1, Image = 2, File = 3, Favorite = 4 };',
        "    enum class Tab : uint8_t { All = 0, Text = 1, Image = 2, File = 3, Favorite = 4 };`r`n    enum class ThemeMode : uint8_t { System = 0, Light = 1, Dark = 2 };"
    )
}

if (-not $src.Contains('inline ThemeMode themeMode')) {
    $src = $src.Replace(
        '    inline std::unordered_set<uint64_t> multiHashes;',
        "    inline std::unordered_set<uint64_t> multiHashes;`r`n    inline ThemeMode themeMode{ ThemeMode::System };`r`n    inline int multiAnchor{ -1 };"
    )
}

# Insert persisted theme helpers immediately before theme().
if (-not $src.Contains('inline std::filesystem::path themeModePath')) {
    $needle = @'
    inline Theme theme()
'@
    $insert = @'
    inline std::filesystem::path themeModePath()
    {
        auto p = storagePath();
        p.replace_filename(L"clipboard_theme.txt");
        return p;
    }

    inline void loadThemeMode()
    {
        std::ifstream f(themeModePath());
        int v = 0;
        if (f >> v) {
            if (v >= 0 && v <= 2) themeMode = (ThemeMode)v;
        }
    }

    inline void saveThemeMode()
    {
        std::ofstream f(themeModePath(), std::ios::trunc);
        if (f) f << (int)themeMode;
    }

    inline bool useDarkTheme()
    {
        if (themeMode == ThemeMode::Dark) return true;
        if (themeMode == ThemeMode::Light) return false;
        return isDarkMode();
    }

    inline const wchar_t* themeModeLabel()
    {
        if (themeMode == ThemeMode::Dark) return L"深色";
        if (themeMode == ThemeMode::Light) return L"浅色";
        return L"自动";
    }

    inline Theme theme()
'@
    if (-not $src.Contains($needle)) { throw 'v0.9.3 theme helper insertion point missing' }
    $src = $src.Replace($needle, $insert)
}
$src = $src.Replace('        if (isDarkMode()) {', '        if (useDarkTheme()) {')

# Replace row rendering with the flatter current-uTools layout: no permanent 4-button
# strip, no left sidebar that moves content, metadata on the bottom line, stable blue outline.
$drawPattern = '(?s)    inline void drawListItem\(const DRAWITEMSTRUCT\* dis\).*?\r?\n    \}\r?\n\r?\n    inline void setTab'
$drawReplacement = @'
    inline RECT expandRect(const RECT& itemRc)
    {
        int center = (itemRc.left + itemRc.right) / 2;
        return { center - 58, itemRc.bottom - 30, center + 58, itemRc.bottom - 5 };
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
        RECT card = rc; InflateRect(&card, -4, -2);
        if (active || multiSelected) fillRect(dis->hDC, card, t.primaryLighter);
        if (active) strokeRoundRect(dis->hDC, card, t.primary, 2, 4);
        else {
            RECT line{ card.left + 12, card.bottom - 1, card.right - 12, card.bottom };
            fillRect(dis->hDC, line, t.textBgLighter);
        }

        RECT body{ card.left + 24, card.top + 8, card.right - 24, card.bottom - 31 };
        bool oversized = false;

        if (item.type == ItemType::Image) {
            RECT imageRc = body;
            imageRc.left += 26; imageRc.right -= 26;
            drawDibFit(dis->hDC, item, imageRc);
        }
        else {
            auto preview = itemPreview(item, oversized);
            if (item.type == ItemType::File) {
                drawText(dis->hDC, preview, body, t.text, uiFont,
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            }
            else {
                drawText(dis->hDC, preview, body, t.text, uiFont,
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            }
        }

        RECT timeRc{ card.left + 24, card.bottom - 29, card.left + 160, card.bottom - 4 };
        drawText(dis->hDC, relativeTime(item.updated), timeRc, t.textLighter, smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        bool expandable = item.type == ItemType::Image || item.type == ItemType::File || oversized;
        if (expandable) {
            RECT er = expandRect(card);
            drawText(dis->hDC, L"⌄  展开", er, active ? t.primary : t.textLighter, smallFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        std::wstring meta;
        if (item.type == ItemType::Image) meta = imageMeta(item);
        else if (item.type == ItemType::File) {
            size_t n = filePaths(item.data).size();
            meta = std::format(L"{} 个文件", n);
        }
        else {
            meta = std::format(L"{} 字符", textFromData(item.data).size());
        }
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
            drawText(dis->hDC, L"★", favRc, t.primary, uiFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void setTab
'@
$patched = [regex]::Replace($src, $drawPattern, $drawReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 row rendering replacement failed' }
$src = $patched

# Replace top chrome with the two-level structure visible in current uTools screenshots:
# first line = clipboard pill + search + explicit theme switch; second line = categories.
$topPattern = '(?s)    inline RECT tabRect\(int index\).*?\r?\n    \}\r?\n\r?\n    inline void drawTopBar\(HDC dc, RECT client\).*?\r?\n    \}\r?\n\r?\n    inline void ensureFonts'
$topReplacement = @'
    inline RECT tabRect(int index)
    {
        RECT cr{};
        if (historyWnd) GetClientRect(historyWnd, &cr);
        int w = std::max(520, (int)(cr.right - cr.left));
        int tabW = std::clamp((w - 20) / 5, 86, 150);
        int x = 10 + index * tabW;
        return { x, 59, x + tabW - 2, 105 };
    }

    inline RECT themeButtonRect(const RECT& client)
    {
        return { std::max(10, (int)client.right - 132), 12, std::max(120, (int)client.right - 12), 49 };
    }

    inline void applyThemeNow()
    {
        Theme t = theme();
        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(t.textBg);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
        if (searchWnd) RedrawWindow(searchWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        if (fullWnd && IsWindowVisible(fullWnd)) InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void cycleThemeMode()
    {
        themeMode = themeMode == ThemeMode::System ? ThemeMode::Light
            : (themeMode == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::System);
        saveThemeMode();
        applyThemeNow();
    }

    inline void drawTopBar(HDC dc, RECT client)
    {
        const Theme t = theme();
        RECT top{ 0,0,client.right,58 };
        fillRect(dc, top, t.navBg);
        RECT tabs{ 0,58,client.right,106 };
        fillRect(dc, tabs, t.navBg);

        RECT pill{ 14,11,154,50 };
        fillRoundRect(dc, pill, t.textBg, 22);
        drawText(dc, L"▣  剪贴板", pill, t.text, boldFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT searchShell{ 166,11,std::max(280, (int)client.right - 146),50 };
        fillRoundRect(dc, searchShell, t.textBg, 10);

        RECT themeRc = themeButtonRect(client);
        fillRoundRect(dc, themeRc, t.navHover, 10);
        std::wstring themeText = std::format(L"◐  {}", themeModeLabel());
        drawText(dc, themeText, themeRc, t.primary, uiFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        size_t favCount = 0;
        for (const auto& it : items) if (it.favorite) ++favCount;
        std::wstring labels[5] = { L"全部", L"文本", L"图像", L"文件",
            favCount ? std::format(L"收藏 ({})", favCount) : L"收藏" };
        for (int i = 0; i < 5; ++i) {
            RECT r = tabRect(i);
            bool selected = (int)activeTab == i;
            if (selected) fillRect(dc, r, t.bg);
            drawText(dc, labels[i], r, selected ? t.text : t.textLighter,
                selected ? boldFont : uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (selected) {
                RECT accent{ r.left + 18, r.bottom - 3, r.right - 18, r.bottom };
                fillRect(dc, accent, t.primary);
            }
        }
        RECT separator{ 0,105,client.right,106 };
        fillRect(dc, separator, t.textBgLighter);

        if (multiMode) {
            RECT multiBg{ 166,11,std::max(520, (int)client.right - 146),50 };
            fillRoundRect(dc, multiBg, t.textBg, 10);
            RECT count{ multiBg.left + 10,15,multiBg.left + 90,47 };
            drawText(dc, std::format(L"已选 {}", multiHashes.size()), count, t.primary, boldFont,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT copy{ multiBg.right - 220,14,multiBg.right - 154,48 };
            RECT paste{ multiBg.right - 148,14,multiBg.right - 82,48 };
            RECT exit{ multiBg.right - 76,14,multiBg.right - 10,48 };
            for (auto r : { copy,paste,exit }) fillRoundRect(dc, r, t.navHover, 8);
            drawText(dc, L"复制", copy, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"粘贴", paste, t.primary, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            drawText(dc, L"退出", exit, t.text, uiFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    inline void ensureFonts
'@
$patched = [regex]::Replace($src, $topPattern, $topReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 top chrome replacement failed' }
$src = $patched

# Layout: reserve 106px for the two-level header and keep the full-preview window out
# of the child-control layout because it is now a true top-level owned window.
$layoutPattern = '(?s)    inline void layoutHistory\(HWND hwnd\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline Item\* findByHash'
$layoutReplacement = @'
    inline void layoutHistory(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int w = (int)(rc.right - rc.left), h = (int)(rc.bottom - rc.top);
        if (listWnd) MoveWindow(listWnd, 0, 106, w, std::max(1, h - 106), TRUE);
        if (searchWnd) {
            int x = 182;
            int right = std::max(x + 90, w - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
        if (clearWnd) ShowWindow(clearWnd, SW_HIDE);
    }

    inline Item* findByHash
'@
$patched = [regex]::Replace($src, $layoutPattern, $layoutReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 layout replacement failed' }
$src = $patched

# Fix the broken v0.9.2 preview: WS_CHILD cannot reliably cover sibling list/edit
# windows.  Use a separate owned top-level preview with its own client area instead.
$showPattern = '(?s)    inline void showFull\(int idx\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void hideFull\(\)\r?\n    \{.*?\r?\n    \}'
$showReplacement = @'
    inline void showFull(int idx)
    {
        Item* item = itemAtListIndex(idx);
        if (!item || !historyWnd) return;
        fullItemHash = item->hash;
        fullImageZoom = 1.0;
        const auto hInst = GetModuleHandleW(nullptr);
        if (!fullWnd || !IsWindow(fullWnd)) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int ww = std::min(1000, std::max(680, (int)(work.right - work.left) - 120));
            int wh = std::min(760, std::max(520, (int)(work.bottom - work.top) - 120));
            int x = work.left + ((work.right - work.left) - ww) / 2;
            int y = work.top + ((work.bottom - work.top) - wh) / 2;
            fullWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"WeShotClipboardFullView",
                item->type == ItemType::Image ? L"WeShot 图片预览" : L"WeShot 内容预览",
                WS_OVERLAPPEDWINDOW, x, y, ww, wh, historyWnd, nullptr, hInst, nullptr);
        }
        if (!fullWnd) return;
        ShowWindow(fullWnd, SW_SHOW);
        SetWindowPos(fullWnd, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(fullWnd);
        SetFocus(fullWnd);
        InvalidateRect(fullWnd, nullptr, TRUE);
    }

    inline void hideFull()
    {
        if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
        if (historyWnd && IsWindow(historyWnd)) SetForegroundWindow(historyWnd);
        if (listWnd) SetFocus(listWnd);
    }
'@
$patched = [regex]::Replace($src, $showPattern, $showReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 top-level preview replacement failed' }
$src = $patched

# Official-style input model + right-click action menu.
$listPattern = '(?s)    inline LRESULT CALLBACK listProc\(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline LRESULT CALLBACK fullProc'
$listReplacement = @'
    inline void deleteMultiItems()
    {
        if (multiHashes.empty()) return;
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) {
            return multiHashes.contains(it.hash);
        }), items.end());
        multiHashes.clear(); multiMode = false; multiAnchor = -1;
        totalBytes = 0; for (const auto& it : items) totalBytes += it.data.size();
        saveStore(); refreshList();
    }

    inline void showItemContextMenu(int idx, POINT screenPt)
    {
        Item* item = itemAtListIndex(idx);
        if (!item) return;
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        enum { CMD_PASTE = 5201, CMD_COPY, CMD_PREVIEW, CMD_FAVORITE, CMD_DELETE };
        AppendMenuW(menu, MF_STRING, CMD_PASTE, L"执行粘贴");
        AppendMenuW(menu, MF_STRING, CMD_COPY, L"复制");
        AppendMenuW(menu, MF_STRING, CMD_PREVIEW, item->type == ItemType::Image ? L"预览图片" : L"展开内容");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_FAVORITE, item->favorite ? L"取消收藏" : L"收藏");
        AppendMenuW(menu, MF_STRING, CMD_DELETE, L"删除记录");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            screenPt.x, screenPt.y, 0, historyWnd, nullptr);
        DestroyMenu(menu);
        if (cmd == CMD_PASTE) useListItem(idx, true);
        else if (cmd == CMD_COPY) useListItem(idx, false);
        else if (cmd == CMD_PREVIEW) showFull(idx);
        else if (cmd == CMD_FAVORITE) toggleFavorite(idx);
        else if (cmd == CMD_DELETE) deleteListItem(idx);
    }

    inline void selectRangeTo(int idx)
    {
        if (idx < 0 || idx >= (int)visibleItems.size()) return;
        if (multiAnchor < 0) multiAnchor = currentListIndex() >= 0 ? currentListIndex() : idx;
        multiMode = true; multiHashes.clear();
        int a = std::min(multiAnchor, idx), b = std::max(multiAnchor, idx);
        for (int i = a; i <= b; ++i) {
            if (auto* it = itemAtListIndex(i)) multiHashes.insert(it->hash);
        }
        SendMessageW(listWnd, LB_SETCURSEL, idx, 0);
        if (searchWnd) ShowWindow(searchWnd, SW_HIDE);
        if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE);
        InvalidateRect(listWnd, nullptr, TRUE);
    }

    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_LBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (HIWORD(hit) || idx < 0 || idx >= (int)visibleItems.size()) return 0;
            RECT itemRc{}; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRc);
            RECT er = expandRect(itemRc);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (shift) { selectRangeTo(idx); return 0; }
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            multiAnchor = idx;
            if (pointIn(er, p)) { showFull(idx); return 0; }
            // Current official uTools behavior: a single click only selects.
            if (multiMode) { multiMode = false; multiHashes.clear(); if (searchWnd) ShowWindow(searchWnd, SW_SHOW); }
            InvalidateRect(hwnd, &itemRc, FALSE);
            if (historyWnd) InvalidateRect(historyWnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                useListItem(idx, true);
            }
            return 0;
        }
        case WM_RBUTTONUP:
        {
            DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            int idx = LOWORD(hit);
            if (!HIWORD(hit) && idx >= 0 && idx < (int)visibleItems.size()) {
                SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
                POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &p);
                showItemContextMenu(idx, p);
            }
            return 0;
        }
        case WM_CHAR:
        {
            if (wParam >= 0x20 && wParam != 0x7f && searchWnd && !multiMode) {
                SetFocus(searchWnd);
                SendMessageW(searchWnd, WM_CHAR, wParam, lParam);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
        {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int idx = currentListIndex();
            if (wParam == VK_ESCAPE) {
                if (multiMode) {
                    multiMode = false; multiHashes.clear(); if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
                    if (historyWnd) InvalidateRect(historyWnd, nullptr, TRUE); InvalidateRect(hwnd, nullptr, TRUE);
                }
                else if (historyWnd) ShowWindow(historyWnd, SW_HIDE);
                return 0;
            }
            if (wParam == VK_RETURN) { if (multiMode) copyMulti(true); else useListItem(idx, true); return 0; }
            if (wParam == VK_DELETE) { if (multiMode) deleteMultiItems(); else deleteListItem(idx); return 0; }
            if (wParam == VK_SPACE) { toggleMultiCurrent(); if (multiAnchor < 0) multiAnchor = idx; return 0; }
            if (wParam == VK_TAB) {
                int next = idx + (shift ? -1 : 1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, next, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                multiAnchor = next; return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                int t = (int)activeTab + (wParam == VK_RIGHT ? 1 : -1);
                if (t < 0) t = 4; if (t > 4) t = 0;
                setTab((Tab)t); return 0;
            }
            if (wParam == VK_OEM_2 && !ctrl) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && (wParam == 'F' || wParam == 'L')) { if (searchWnd) SetFocus(searchWnd); return 0; }
            if (ctrl && wParam == 'C') { if (multiMode) copyMulti(false); else useListItem(idx, false); return 0; }
            if (ctrl && (wParam == 'J' || wParam == 'K')) {
                int next = idx + (wParam == 'J' ? 1 : -1);
                next = std::clamp(next, 0, std::max(0, (int)visibleItems.size() - 1));
                SendMessageW(hwnd, LB_SETCURSEL, next, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, std::max(0, next - 3), 0);
                multiAnchor = next; return 0;
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
if ($patched -eq $src) { throw 'v0.9.3 interaction replacement failed' }
$src = $patched

# Preview paint/message loop: whole client is usable; no intentional black remainder.
$fullPattern = '(?s)    inline LRESULT CALLBACK fullProc\(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void clearAll'
$fullReplacement = @'
    inline LRESULT CALLBACK fullProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_MOUSEWHEEL:
        {
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                fullImageZoom = std::clamp(fullImageZoom + steps * 0.25, 1.0, 5.0);
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { hideFull(); return 0; }
            if (wParam == VK_RETURN) { restoreByHash(fullItemHash, true); return 0; }
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            if (wParam == '0') { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            break;
        case WM_LBUTTONDOWN:
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            RECT zoomOut{ rc.right - 330,12,rc.right - 286,50 };
            RECT zoomReset{ rc.right - 278,12,rc.right - 206,50 };
            RECT zoomIn{ rc.right - 198,12,rc.right - 154,50 };
            RECT copy{ rc.right - 146,12,rc.right - 102,50 };
            RECT close{ rc.right - 94,12,rc.right - 18,50 };
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            auto* item = findByHash(fullItemHash);
            if (item && item->type == ItemType::Image) {
                if (pointIn(zoomOut, p)) { fullImageZoom = std::clamp(fullImageZoom - .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomReset, p)) { fullImageZoom = 1.0; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                if (pointIn(zoomIn, p)) { fullImageZoom = std::clamp(fullImageZoom + .25, 1.0, 5.0); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            }
            if (pointIn(copy, p)) { restoreByHash(fullItemHash, false); return 0; }
            if (pointIn(close, p)) { hideFull(); return 0; }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            Theme t = theme();
            fillRect(dc, rc, t.bg);
            auto* item = findByHash(fullItemHash);
            if (item) {
                RECT heading{ 20,10,rc.right - 350,52 };
                std::wstring title = item->type == ItemType::Image ? L"图片预览" : L"完整内容";
                if (item->type == ItemType::Image) title += L"  ·  滚轮 / +/- 缩放";
                drawText(dc, title, heading, t.text, boldFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT zoomOut{ rc.right - 330,12,rc.right - 286,50 };
                RECT zoomReset{ rc.right - 278,12,rc.right - 206,50 };
                RECT zoomIn{ rc.right - 198,12,rc.right - 154,50 };
                RECT copy{ rc.right - 146,12,rc.right - 102,50 };
                RECT close{ rc.right - 94,12,rc.right - 18,50 };
                if (item->type == ItemType::Image) {
                    for (auto r : { zoomOut,zoomReset,zoomIn }) fillRoundRect(dc, r, t.navHover, 8);
                    drawText(dc, L"−", zoomOut, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    std::wstring z = fullImageZoom <= 1.001 ? L"适应" : std::format(L"{}%", (int)std::lround(fullImageZoom * 100.0));
                    drawText(dc, z, zoomReset, t.primary, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    drawText(dc, L"+", zoomIn, t.primary, boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                fillRoundRect(dc, copy, t.navHover, 8); fillRoundRect(dc, close, t.textBg, 8);
                drawText(dc, L"复制", copy, t.primary, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                drawText(dc, L"关闭", close, t.text, smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                RECT content{ 18,62,rc.right - 18,rc.bottom - 18 };
                fillRoundRect(dc, content, t.textBg, 8);
                strokeRoundRect(dc, content, t.textBgLighter, 1, 8);
                RECT inner = content; InflateRect(&inner, -18, -18);
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
if ($patched -eq $src) { throw 'v0.9.3 preview loop replacement failed' }
$src = $patched

# Replace the history window proc so search, row heights, tabs and the theme switch all
# match the new chrome and work at narrow widths too.
$historyPattern = '(?s)    inline LRESULT CALLBACK historyProc\(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline LRESULT CALLBACK listenerProc'
$historyReplacement = @'
    inline LRESULT CALLBACK historyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE:
        {
            historyWnd = hwnd; ensureFonts(hwnd);
            Theme t = theme(); editBrush = CreateSolidBrush(t.textBg);
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
                oldListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(listWnd, GWLP_WNDPROC, (LONG_PTR)listProc));
            }
            if (searchWnd) {
                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索...");
            }
            refreshList(); layoutHistory(hwnd); return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto p = reinterpret_cast<MINMAXINFO*>(lParam);
            p->ptMinTrackSize.x = 520; p->ptMinTrackSize.y = 420; return 0;
        }
        case WM_SIZE: layoutHistory(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wParam; Theme t = theme();
            SetTextColor(dc, t.text); SetBkColor(dc, t.textBg);
            return (LRESULT)editBrush;
        }
        case WM_MEASUREITEM:
        {
            auto mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlID == ID_LIST) {
                if (mi->itemID < visibleItems.size()) {
                    const auto& it = items[visibleItems[mi->itemID]];
                    if (it.type == ItemType::Image) mi->itemHeight = 210;
                    else if (it.type == ItemType::File) mi->itemHeight = filePaths(it.data).size() > 3 ? 160 : 112;
                    else { bool over = false; itemPreview(it, over); mi->itemHeight = over ? 150 : 96; }
                }
                else mi->itemHeight = 96;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == ID_LIST) { drawListItem(dis); return TRUE; }
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
            if (p.y < 58 && pointIn(themeButtonRect(rc), p)) { cycleThemeMode(); return 0; }
            if (p.y >= 58 && p.y < 106) {
                for (int i = 0; i < 5; ++i) if (pointIn(tabRect(i), p)) { setTab((Tab)i); return 0; }
            }
            if (multiMode && p.y < 58) {
                RECT multiBg{ 166,11,std::max(520, (int)rc.right - 146),50 };
                RECT copy{ multiBg.right - 220,14,multiBg.right - 154,48 };
                RECT paste{ multiBg.right - 148,14,multiBg.right - 82,48 };
                RECT exit{ multiBg.right - 76,14,multiBg.right - 10,48 };
                if (pointIn(copy, p)) { copyMulti(false); return 0; }
                if (pointIn(paste, p)) { copyMulti(true); return 0; }
                if (pointIn(exit, p)) {
                    multiMode = false; multiHashes.clear(); multiAnchor = -1;
                    if (searchWnd) ShowWindow(searchWnd, SW_SHOW);
                    InvalidateRect(hwnd, nullptr, TRUE); if (listWnd) InvalidateRect(listWnd, nullptr, TRUE); return 0;
                }
            }
            break;
        }
        case WM_SETTINGCHANGE:
            if (themeMode == ThemeMode::System) applyThemeNow();
            return 0;
        case WM_CLOSE:
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(hwnd, SW_HIDE); return 0;
        case WM_DESTROY:
            historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr; fullWnd = nullptr; oldListProc = nullptr;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{}; GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme().bg); drawTopBar(dc, rc);
            EndPaint(hwnd, &ps); return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    inline LRESULT CALLBACK listenerProc
'@
$patched = [regex]::Replace($src, $historyPattern, $historyReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 history window replacement failed' }
$src = $patched

# Load the user-selected clipboard theme and make the toggle hotkey close the preview too.
$src = $src.Replace('        loadStore();', "        loadStore();`r`n        loadThemeMode();")
$togglePattern = '(?s)    inline void toggle\(\)\r?\n    \{.*?\r?\n    \}\r?\n\r?\n    inline void dispose'
$toggleReplacement = @'
    inline void toggle()
    {
        if (historyWnd && IsWindow(historyWnd) && IsWindowVisible(historyWnd)) {
            if (fullWnd && IsWindow(fullWnd)) ShowWindow(fullWnd, SW_HIDE);
            ShowWindow(historyWnd, SW_HIDE); return;
        }
        show();
    }

    inline void dispose
'@
$patched = [regex]::Replace($src, $togglePattern, $toggleReplacement, 1)
if ($patched -eq $src) { throw 'v0.9.3 toggle replacement failed' }
$src = $patched

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @(
    'ThemeMode::System',
    'clipboard_theme.txt',
    'single click only selects',
    'WM_LBUTTONDBLCLK',
    'showItemContextMenu',
    'WS_OVERLAPPEDWINDOW',
    'WeShot 图片预览',
    'mi->itemHeight = 210;',
    '◐  {}',
    'p->ptMinTrackSize.x = 520'
)) {
    if (-not $verify.Contains($needle)) { throw "v0.9.3 clipboard verification failed: $needle" }
}
Write-Host 'v0.9.3 official-style clipboard applied: top-level preview, uTools input model, flat rows, explicit theme switch.'
