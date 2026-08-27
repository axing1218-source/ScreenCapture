$ErrorActionPreference = 'Stop'

# Corrected v0.9.5 patch.
$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw
$src = $src.Replace('// WeShot clipboard manager v0.9.4', '// WeShot clipboard manager v0.9.5')

# Include existing WeShot settings entry point with PowerShell-safe quoting.
if (-not $src.Contains('#include "Win/WinSetting.h"')) {
$inc = @'
#include <vector>
#include "Win/WinSetting.h"
'@
    $src = $src.Replace('#include <vector>', $inc.TrimEnd("`r","`n"))
}

if (-not $src.Contains('inline WNDPROC oldSearchProc')) {
    $src = $src.Replace(
        '    inline WNDPROC oldListProc{ nullptr };',
        "    inline WNDPROC oldListProc{ nullptr };`r`n    inline WNDPROC oldSearchProc{ nullptr };`r`n    inline bool sidePinned{ false };"
    )
}
if (-not $src.Contains('inline LRESULT CALLBACK searchProc(HWND, UINT, WPARAM, LPARAM);')) {
    $src = $src.Replace(
        '    inline LRESULT CALLBACK listProc(HWND, UINT, WPARAM, LPARAM);',
        "    inline LRESULT CALLBACK listProc(HWND, UINT, WPARAM, LPARAM);`r`n    inline LRESULT CALLBACK searchProc(HWND, UINT, WPARAM, LPARAM);"
    )
}

# Right-side uTools-like rail.
if (-not $src.Contains('inline int sideRailWidth()')) {
$rail = @'
    inline int sideRailWidth() { return 68; }

    inline RECT sideRailButtonRect(const RECT& client, int index)
    {
        const int railLeft = client.right - sideRailWidth();
        const int cx = railLeft + sideRailWidth() / 2;
        if (index < 3) {
            const int cy = 40 + index * 66;
            return { cx - 25, cy - 25, cx + 25, cy + 25 };
        }
        const int cy = client.bottom - 148 + (index - 3) * 54;
        return { cx - 24, cy - 22, cx + 24, cy + 22 };
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
            SelectObject(dc, b);
            Polygon(dc, p, 3);
            SelectObject(dc, oldBrush);
            DeleteObject(b);
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
        const int railLeft = client.right - sideRailWidth();
        RECT rail{ railLeft, 0, client.right, client.bottom };
        fillRect(dc, rail, RGB(0x2f,0x30,0x32));

        for (int i = 0; i < 3; ++i) {
            RECT r = sideRailButtonRect(client, i);
            HBRUSH brush = CreateSolidBrush(RGB(0x88,0xca,0xf6));
            HGDIOBJ oldB = SelectObject(dc, brush);
            HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldP); SelectObject(dc, oldB); DeleteObject(brush);
            drawRailGlyph(dc, i, r, RGB(0x17,0x2b,0x39));
        }
        for (int i = 3; i < 6; ++i) {
            RECT r = sideRailButtonRect(client, i);
            drawRailGlyph(dc, i, r, RGB(0xf2,0xf2,0xf2));
        }
    }

'@
    $anchor = '    inline RECT tabRect(int index)'
    if (-not $src.Contains($anchor)) { throw 'v0.9.5 rail anchor missing' }
    $src = $src.Replace($anchor, $rail + $anchor)
}

# Do not let content overlap the new rail.
$src = $src.Replace(
    '        int w = std::max(520, (int)(cr.right - cr.left));',
    '        int w = std::max(520, (int)(cr.right - cr.left) - sideRailWidth());'
)

$oldLayout = @'
        const int w = (int)(rc.right - rc.left), h = (int)(rc.bottom - rc.top);
        if (listWnd) MoveWindow(listWnd, 0, 106, w, std::max(1, h - 106), TRUE);
        if (searchWnd) {
            int x = 30;
            int right = std::max(x + 90, w - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
'@
$newLayout = @'
        const int w = (int)(rc.right - rc.left), h = (int)(rc.bottom - rc.top);
        const int contentW = std::max(1, w - sideRailWidth());
        if (listWnd) MoveWindow(listWnd, 0, 106, contentW, std::max(1, h - 106), TRUE);
        if (searchWnd) {
            int x = 30;
            int right = std::max(x + 90, contentW - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
'@
if (-not $src.Contains($oldLayout)) { throw 'v0.9.5 layout target missing' }
$src = $src.Replace($oldLayout, $newLayout)

# Reliable custom placeholder.
if (-not $src.Contains('inline LRESULT CALLBACK searchProc(HWND hwnd')) {
$proc = @'
    inline LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            if (historyWnd) ShowWindow(historyWnd, SW_HIDE);
            return 0;
        }
        if (msg == WM_PAINT) {
            LRESULT result = oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            if (GetWindowTextLengthW(hwnd) == 0) {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    RECT r{}; GetClientRect(hwnd, &r); r.left += 7; r.right -= 7;
                    drawText(dc, L"搜索...", r, theme().textLighter, uiFont,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    ReleaseDC(hwnd, dc);
                }
            }
            return result;
        }
        if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
            LRESULT result = oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }
        return oldSearchProc ? CallWindowProcW(oldSearchProc, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

'@
    $anchor = '    inline LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)'
    if (-not $src.Contains($anchor)) { throw 'v0.9.5 search proc anchor missing' }
    $src = $src.Replace($anchor, $proc + $anchor)
}

$fontLine = '                SendMessageW(searchWnd, WM_SETFONT, (WPARAM)uiFont, TRUE);'
if (-not $src.Contains('oldSearchProc = reinterpret_cast<WNDPROC>')) {
    if (-not $src.Contains($fontLine)) { throw 'v0.9.5 search subclass anchor missing' }
    $subclass = '                oldSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(searchWnd, GWLP_WNDPROC, (LONG_PTR)searchProc));'
    $src = $src.Replace($fontLine, $fontLine + "`r`n" + $subclass)
}
$src = $src.Replace('SetTextColor(dc, t.text); SetBkColor(dc, t.bg);', 'SetTextColor(dc, t.text); SetBkColor(dc, t.textBg);')

# Side rail interactions.
$oldClick = @'
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
'@
$newClick = @'
        case WM_LBUTTONDOWN:
        {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT client{}; GetClientRect(hwnd, &client);
            if (p.x >= client.right - sideRailWidth()) {
                for (int i = 0; i < 6; ++i) {
                    RECT br = sideRailButtonRect(client, i);
                    if (!pointIn(br, p)) continue;
                    if (i == 0) {
                        multiMode = !multiMode;
                        if (!multiMode) multiHashes.clear();
                        if (searchWnd) ShowWindow(searchWnd, multiMode ? SW_HIDE : SW_SHOW);
                        InvalidateRect(hwnd, nullptr, TRUE);
                        if (listWnd) InvalidateRect(listWnd, nullptr, TRUE);
                    }
                    else if (i == 1) useListItem(currentListIndex(), true);
                    else if (i == 2) {
                        sidePinned = !sidePinned;
                        SetWindowPos(hwnd, sidePinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                            0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    }
                    else if (i == 3) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    else if (i == 4) WinSetting::init();
                    else if (i == 5) clearAll();
                    return 0;
                }
                return 0;
            }
'@
if (-not $src.Contains($oldClick)) { throw 'v0.9.5 rail click target missing' }
$src = $src.Replace($oldClick, $newClick)

$oldPaint = @'
            RECT rc{}; GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme().bg); drawTopBar(dc, rc);
            EndPaint(hwnd, &ps); return 0;
'@
$newPaint = @'
            RECT rc{}; GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme().bg);
            RECT contentRc = rc;
            contentRc.right = std::max(contentRc.left, contentRc.right - sideRailWidth());
            drawTopBar(dc, contentRc);
            drawSideRail(dc, rc);
            EndPaint(hwnd, &ps); return 0;
'@
if (-not $src.Contains($oldPaint)) { throw 'v0.9.5 paint target missing' }
$src = $src.Replace($oldPaint, $newPaint)

$src = $src.Replace(
    'historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr; fullWnd = nullptr; oldListProc = nullptr;',
    'historyWnd = nullptr; listWnd = nullptr; searchWnd = nullptr; clearWnd = nullptr; fullWnd = nullptr; oldListProc = nullptr; oldSearchProc = nullptr;'
)
Set-Content $path $src -Encoding utf8

$check = Get-Content $path -Raw
foreach ($n in @('drawSideRail', 'drawText(dc, L"搜索..."', 'WinSetting::init();', 'oldSearchProc = reinterpret_cast<WNDPROC>')) {
    if (-not $check.Contains($n)) { throw "v0.9.5 clipboard verify missing: $n" }
}

# -----------------------------------------------------------------------------
# Translation loading continuity.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotCaptureTranslate.h'
$tr = Get-Content $path -Raw

$oldStart = @'
        std::vector<BYTE> pixels; int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) return;
        busy = true;
        cachedX = sx; cachedY = sy; cachedW = width; cachedH = height;
        const auto myRequest = ++requestId;
        const float border = win->cutMask->strokeWidth;

        // Direct screenshot-translation feedback: immediately cover the selected capture with a
        // gray translucent preview and a centered loading message while Gemini is working.
        loadingOverlay = std::make_unique<LoadingOverlay>(sx, sy, width, height, pixels, border);
        loadingOverlay->open();
'@
$newStart = @'
        busy = true;
        const auto myRequest = ++requestId;
        const float border = win->cutMask->strokeWidth;

        // Dim first, then do the CPU bitmap copy. This removes the short clear flash
        // immediately after clicking Translate.
        loadingOverlay = std::make_unique<LoadingOverlay>(sx, sy, sw, sh, std::vector<BYTE>{}, border);
        loadingOverlay->open();

        std::vector<BYTE> pixels; int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) {
            if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
            busy = false;
            return;
        }
        cachedX = sx; cachedY = sy; cachedW = width; cachedH = height;
'@
if (-not $tr.Contains($oldStart)) { throw 'v0.9.5 loading start target missing' }
$tr = $tr.Replace($oldStart, $newStart)

$oldFinish = @'
                if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
                busy = false;
                if (!result.ok) {
                    auto msg = result.error.empty() ? std::wstring(L"Gemini 翻译失败。") : result.error;
                    MessageBoxW(win->hwnd, msg.c_str(), L"WeShot 翻译", MB_OK | MB_ICONWARNING);
                    return;
                }
                overlay = std::make_unique<TranslationOverlay>(sx, sy, width, height,
                    std::move(sourcePixels), std::move(result.blocks), border, win);
                overlay->open();
                ready = true; showing = true;
'@
$newFinish = @'
                busy = false;
                if (!result.ok) {
                    if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
                    auto msg = result.error.empty() ? std::wstring(L"Gemini 翻译失败。") : result.error;
                    MessageBoxW(win->hwnd, msg.c_str(), L"WeShot 翻译", MB_OK | MB_ICONWARNING);
                    return;
                }
                overlay = std::make_unique<TranslationOverlay>(sx, sy, width, height,
                    std::move(sourcePixels), std::move(result.blocks), border, win);
                overlay->open();
                // Only remove the dim layer after the translated window is actually open.
                if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
                ready = true; showing = true;
'@
if (-not $tr.Contains($oldFinish)) { throw 'v0.9.5 loading finish target missing' }
$tr = $tr.Replace($oldFinish, $newFinish)
Set-Content $path $tr -Encoding utf8

$check = Get-Content $path -Raw
foreach ($n in @('LoadingOverlay>(sx, sy, sw, sh, std::vector<BYTE>{}, border)', 'Only remove the dim layer after the translated window is actually open.')) {
    if (-not $check.Contains($n)) { throw "v0.9.5 translation verify missing: $n" }
}

Write-Host 'v0.9.5 corrected patch applied.'
