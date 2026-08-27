$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# WeShot v0.9.4
# 1) Clipboard chrome: follow the current uTools clipboard screenshots more
#    literally.  WeShot is already inside its own clipboard window, so the uTools
#    host's left "剪贴板" identity pill is redundant here.  Give that space to the
#    search field, keep the theme mode control on the right, and use the same terse
#    "搜索..." affordance that makes the field self-explanatory on first use.
# 2) Direct screenshot translation: make the translated overlay keyboard-active
#    and let Escape leave the whole capture just like clicking the capture X.
# -----------------------------------------------------------------------------

$clipboardPath = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $clipboardPath -Raw

$src = $src.Replace('// WeShot clipboard manager v0.9.3', '// WeShot clipboard manager v0.9.4')

$oldChrome = @'
        RECT pill{ 14,11,154,50 };
        fillRoundRect(dc, pill, t.textBg, 22);
        drawText(dc, L"▣  剪贴板", pill, t.text, boldFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT searchShell{ 166,11,std::max(280, (int)client.right - 146),50 };
        fillRoundRect(dc, searchShell, t.textBg, 10);
'@
$newChrome = @'
        // uTools shows a "剪贴板" chip here because its top bar belongs to the host.
        // WeShot is already the clipboard window, so keep only the actual search field.
        RECT searchShell{ 16,11,std::max(130, (int)client.right - 146),50 };
        fillRoundRect(dc, searchShell, t.textBg, 10);
'@
if (-not $src.Contains($oldChrome)) { throw 'v0.9.4 clipboard top search target missing' }
$src = $src.Replace($oldChrome, $newChrome)

# The multi-select toolbar occupies the same search lane; move it with the search.
$src = $src.Replace(
    '            RECT multiBg{ 166,11,std::max(520, (int)client.right - 146),50 };',
    '            RECT multiBg{ 16,11,std::max(130, (int)client.right - 146),50 };'
)

$oldLayout = @'
        if (searchWnd) {
            int x = 182;
            int right = std::max(x + 90, w - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
'@
$newLayout = @'
        if (searchWnd) {
            int x = 30;
            int right = std::max(x + 90, w - 148);
            MoveWindow(searchWnd, x, 17, std::max(90, right - x - 8), 28, TRUE);
        }
'@
if (-not $src.Contains($oldLayout)) { throw 'v0.9.4 clipboard search layout target missing' }
$src = $src.Replace($oldLayout, $newLayout)

# First-use affordance: show the same short placeholder as uTools instead of a blank
# edit box / verbose diagnostics wording.
$src = $src.Replace('L"🔍 检索剪贴板历史..."', 'L"搜索..."')

$cueNeedle = @'
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索...");
'@
if ($src.Contains($cueNeedle) -and -not $src.Contains('EM_SETMARGINS, EC_LEFTMARGIN')) {
    $src = $src.Replace($cueNeedle, @'
                SendMessageW(searchWnd, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索...");
                SendMessageW(searchWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(7, 7));
'@)
}

# Keep the category row visually close to the official screenshots: icon + text,
# while staying on fonts already shipped with Windows (no extra icon assets).
$oldLabels = @'
        std::wstring labels[5] = { L"全部", L"文本", L"图像", L"文件",
            favCount ? std::format(L"收藏 ({})", favCount) : L"收藏" };
'@
$newLabels = @'
        std::wstring labels[5] = { L"▣  全部", L"T  文本", L"▧  图像", L"▱  文件",
            favCount ? std::format(L"★  收藏 ({})", favCount) : L"★  收藏" };
'@
if (-not $src.Contains($oldLabels)) { throw 'v0.9.4 clipboard category labels target missing' }
$src = $src.Replace($oldLabels, $newLabels)

Set-Content $clipboardPath $src -Encoding utf8

# -----------------------------------------------------------------------------
# Direct translation Escape handling.
# The v0.8.x/v0.9.x translated overlay was WS_EX_NOACTIVATE, so depending on
# which capture/tool window retained focus, Escape could end up at neither visible
# translated surface.  Make only the completed translation overlay activatable;
# it remains mouse-transparent, but now reliably owns keyboard focus while shown.
# -----------------------------------------------------------------------------
$translatePath = 'Src\WeShotCaptureTranslate.h'
$tr = Get-Content $translatePath -Raw

$oldCtor = @'
        TranslationOverlay(int screenX, int screenY, int imageW, int imageH,
            std::vector<BYTE> pixels, std::vector<GeminiClient::TranslationBlock> blocks, float borderWidth)
            : pixels(std::move(pixels)), imageW(imageW), imageH(imageH), blocks(std::move(blocks)), borderWidth(borderWidth)
        {
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
        }

        void open()
        {
            createNativeWindow(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, WS_POPUP);
        }
'@
$newCtor = @'
        TranslationOverlay(int screenX, int screenY, int imageW, int imageH,
            std::vector<BYTE> pixels, std::vector<GeminiClient::TranslationBlock> blocks, float borderWidth,
            WinCap* captureOwner)
            : pixels(std::move(pixels)), imageW(imageW), imageH(imageH), blocks(std::move(blocks)),
              borderWidth(borderWidth), captureOwner(captureOwner)
        {
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
            onKeyDown.add([this](UINT key) {
                if (key != VK_ESCAPE) return;
                auto* target = captureOwner;
                hide();
                // Do not destroy this overlay from inside its own key callback.  Queue the
                // capture close; WinCap's destroy hook then resets the translation state.
                Ling::App::get()->dq.TryEnqueue([target]() {
                    if (target && WinCap::get() == target) target->close();
                });
            });
        }

        void open()
        {
            // Keep click-through behaviour, but allow the completed translation surface
            // to receive Escape.  Loading remains NOACTIVATE and unchanged.
            createNativeWindow(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, WS_POPUP);
            if (hwnd) {
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
            }
        }
'@
if (-not $tr.Contains($oldCtor)) { throw 'v0.9.4 translation overlay constructor target missing' }
$tr = $tr.Replace($oldCtor, $newCtor)

$oldMember = @'
        float borderWidth{ 0.f };
        Ling::Canvas* canvas{ nullptr };
'@
$newMember = @'
        float borderWidth{ 0.f };
        WinCap* captureOwner{ nullptr };
        Ling::Canvas* canvas{ nullptr };
'@
# There are two overlay classes with borderWidth/canvas members.  Replace the last
# occurrence, which belongs to TranslationOverlay, rather than LoadingOverlay.
$memberPos = $tr.LastIndexOf($oldMember)
if ($memberPos -lt 0) { throw 'v0.9.4 translation owner member target missing' }
$tr = $tr.Substring(0, $memberPos) + $newMember + $tr.Substring($memberPos + $oldMember.Length)

$oldMake = @'
                overlay = std::make_unique<TranslationOverlay>(sx, sy, width, height,
                    std::move(sourcePixels), std::move(result.blocks), border);
'@
$newMake = @'
                overlay = std::make_unique<TranslationOverlay>(sx, sy, width, height,
                    std::move(sourcePixels), std::move(result.blocks), border, win);
'@
if (-not $tr.Contains($oldMake)) { throw 'v0.9.4 translation overlay creation target missing' }
$tr = $tr.Replace($oldMake, $newMake)

Set-Content $translatePath $tr -Encoding utf8

# Fail CI immediately if a preceding patch changes these assumptions.
$clipVerify = Get-Content $clipboardPath -Raw
foreach ($needle in @(
    'RECT searchShell{ 16,11',
    'L"搜索..."',
    'int x = 30;',
    'L"▣  全部"',
    'themeModeLabel()'
)) {
    if (-not $clipVerify.Contains($needle)) { throw "v0.9.4 clipboard verification failed: $needle" }
}
if ($clipVerify.Contains('L"▣  剪贴板"')) { throw 'v0.9.4 clipboard identity pill was not removed' }

$trVerify = Get-Content $translatePath -Raw
foreach ($needle in @(
    'WinCap* captureOwner',
    'if (key != VK_ESCAPE) return;',
    'WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT',
    'std::move(result.blocks), border, win'
)) {
    if (-not $trVerify.Contains($needle)) { throw "v0.9.4 translation Escape verification failed: $needle" }
}

Write-Host 'v0.9.4 applied: uTools-style search-first header + reliable Esc exit from direct translated capture.'
