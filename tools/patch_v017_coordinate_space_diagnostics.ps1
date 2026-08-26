$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# v0.8.17
# Fix Gemini occasionally returning source-pixel box coordinates even though the
# prompt requests normalized 0..1000 coordinates.  We do NOT use screenshot-height
# buckets.  For every region, evaluate both coordinate interpretations and choose
# the one whose horizontal and vertical source-font estimates agree better.
# Also add source-level diagnostics so logging no longer depends on WHLOG.dll.

# -----------------------------------------------------------------------------
# 1) Source-level diagnostic helper.  Numeric/layout metadata only: never text,
#    API keys, image bytes, OCR content or translations.
# -----------------------------------------------------------------------------
$diagPath = 'Src\WeShotDiag.h'
$diag = @'
#pragma once
#include <Windows.h>
#include <string>
#include <format>

namespace WeShotDiag
{
    inline std::wstring logPath()
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p.resize(pos + 1); else p.clear();
        return p + L"WeShot_Diagnostics.log";
    }

    inline void append(const std::wstring& message)
    {
        SYSTEMTIME st{}; GetLocalTime(&st);
        auto line = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
        int n = WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return;
        std::string utf8((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), utf8.data(), n, nullptr, nullptr);
        auto path = logPath();
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0; WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(h);
    }
}
'@
Set-Content $diagPath $diag -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Shared replacement renderer for direct overlay and OCR result window.
# -----------------------------------------------------------------------------
$renderer = @'
        void __FUNC__(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            if (!ctx || __EMPTY__) return;
            const float dw = imageRect.right - imageRect.left;
            const float dh = imageRect.bottom - imageRect.top;

            auto glyphUnits = [](const std::wstring& text) {
                float units = 0.f;
                for (wchar_t ch : text) {
                    if (ch == L'\r' || ch == L'\n') continue;
                    if (ch == L'\t') { units += 1.2f; continue; }
                    if (iswspace(ch)) { units += .32f; continue; }
                    if (ch >= 0x2E80) { units += 1.f; continue; }
                    if (iswalnum(ch)) { units += .55f; continue; }
                    units += .38f;
                }
                return std::max(.75f, units);
            };

            auto maxLineUnits = [&](const GeminiClient::TranslationBlock& block) {
                const auto& src = block.source.empty() ? block.translation : block.source;
                float maxU = 0.f, cur = 0.f;
                bool hadBreak = false;
                for (wchar_t ch : src) {
                    if (ch == L'\r') continue;
                    if (ch == L'\n') { maxU = std::max(maxU, cur); cur = 0.f; hadBreak = true; continue; }
                    std::wstring one(1, ch); cur += glyphUnits(one);
                }
                maxU = std::max(maxU, cur);
                const int lines = std::max(1, block.sourceLines);
                if (!hadBreak && lines > 1) maxU = std::max(.75f, glyphUnits(src) / (float)lines * 1.08f);
                return std::max(.75f, maxU);
            };

            struct BoxMap { float l, t, r, b; bool pixels; float scoreNorm, scorePixel; };
            auto mapBox = [&](const GeminiClient::TranslationBlock& block) {
                const float rawW = (float)std::max(1, block.xmax - block.xmin);
                const float rawH = (float)std::max(1, block.ymax - block.ymin);
                const int lines = std::max(1, block.sourceLines);
                const float lineU = maxLineUnits(block);

                auto consistency = [&](float sourceW, float sourceH) {
                    const float fontW = sourceW / lineU;
                    const float fontH = sourceH / (1.18f * (float)lines);
                    const float a = std::max(.001f, fontW), b = std::max(.001f, fontH);
                    return std::fabs(std::log(a / b));
                };

                const float normW = (float)imageW * rawW / 1000.f;
                const float normH = (float)imageH * rawH / 1000.f;
                const float scoreN = consistency(normW, normH);

                const bool pixelPossible = block.xmin >= 0 && block.ymin >= 0 &&
                    block.xmax <= imageW + std::max(3, imageW / 20) &&
                    block.ymax <= imageH + std::max(3, imageH / 20);
                const float scoreP = pixelPossible ? consistency(rawW, rawH) : 999.f;

                // Normalized coordinates remain the default because that is the API contract.
                // Switch to pixel-space only when its geometry/content consistency is clearly
                // better.  This is scale inference, not a screenshot-size special case.
                const bool usePixels = pixelPossible && scoreP + .28f < scoreN;
                const float xDen = usePixels ? (float)std::max(1, imageW) : 1000.f;
                const float yDen = usePixels ? (float)std::max(1, imageH) : 1000.f;
                BoxMap m{
                    imageRect.left + dw * block.xmin / xDen,
                    imageRect.top + dh * block.ymin / yDen,
                    imageRect.left + dw * block.xmax / xDen,
                    imageRect.top + dh * block.ymax / yDen,
                    usePixels, scoreN, scoreP
                };
                m.l = std::clamp(m.l, imageRect.left, imageRect.right);
                m.r = std::clamp(m.r, imageRect.left, imageRect.right);
                m.t = std::clamp(m.t, imageRect.top, imageRect.bottom);
                m.b = std::clamp(m.b, imageRect.top, imageRect.bottom);
                return m;
            };

            auto estimateSourceFont = [&](const GeminiClient::TranslationBlock& block, float boxW, float boxH) {
                const auto& sourceText = block.source.empty() ? block.translation : block.source;
                const float units = glyphUnits(sourceText);
                const float areaFont = std::sqrt(std::max(.01f, boxW * boxH) / (units * 1.18f));
                const int reportedLines = std::max(1, block.sourceLines);
                float lineFont = boxH / (1.18f * (float)reportedLines);
                lineFont = std::clamp(lineFont, areaFont * .62f, areaFont * 1.55f);
                return std::max(.01f, areaFont * .72f + lineFont * .28f);
            };

            std::vector<float> bodyFonts;
            bodyFonts.reserve(__BLOCKS__.size());
            for (const auto& block : __BLOCKS__) {
                auto m = mapBox(block);
                const float boxW = m.r - m.l, boxH = m.b - m.t;
                if (boxW < .5f || boxH < .5f) continue;
                if (block.role.empty() || block.role == L"body") bodyFonts.push_back(estimateSourceFont(block, boxW, boxH));
            }
            float bodyFont = 0.f;
            if (!bodyFonts.empty()) {
                std::sort(bodyFonts.begin(), bodyFonts.end());
                bodyFont = bodyFonts[bodyFonts.size() / 2];
            }

            auto fits = [](const std::wstring& text, float fs, float width, float height) {
                auto layout = Ling::D2D::makeTextLayout(text, fs, width, 8192.f);
                if (!layout) return false;
                DWRITE_TEXT_METRICS metrics{};
                return SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.height <= height + .5f;
            };

            if (!layoutDiagLogged) {
                WeShotDiag::append(std::format(L"layout path=__PATH__ image={}x{} display={:.1f}x{:.1f} blocks={}",
                    imageW, imageH, dw, dh, __BLOCKS__.size()));
            }

            size_t diagIndex = 0;
            for (const auto& block : __BLOCKS__) {
                auto m = mapBox(block);
                D2D1_RECT_F rect{ m.l, m.t, m.r, m.b };
                const float boxW = rect.right - rect.left;
                const float boxH = rect.bottom - rect.top;
                if (boxW < .5f || boxH < .5f) { ++diagIndex; continue; }

                auto bgColor = sampleBackground(block);
                const float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto textColor = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, textBrush;
                ctx->CreateSolidColorBrush(bgColor, bgBrush.GetAddressOf());
                ctx->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
                if (!bgBrush || !textBrush) { ++diagIndex; continue; }
                ctx->FillRectangle(rect, bgBrush.Get());

                float targetFont = estimateSourceFont(block, boxW, boxH);
                if (bodyFont > 0.f) {
                    if (block.role.empty() || block.role == L"body") targetFont = bodyFont * .72f + targetFont * .28f;
                    else if (block.role == L"title") targetFont = std::max(targetFont, bodyFont * 1.30f);
                    else if (block.role == L"heading") targetFont = std::max(targetFont, bodyFont * 1.15f);
                    else if (block.role == L"caption") targetFont = std::min(targetFont, bodyFont * .92f);
                }

                const float padX = std::min(boxW * .035f, targetFont * .18f);
                const float padY = std::min(boxH * .06f, targetFont * .10f);
                const float innerW = std::max(.5f, boxW - padX * 2.f);
                const float innerH = std::max(.5f, boxH - padY * 2.f);

                float fontSize = targetFont;
                if (!fits(block.translation, fontSize, innerW, innerH)) {
                    float low = targetFont * .18f;
                    while (low > .01f && !fits(block.translation, low, innerW, innerH)) low *= .5f;
                    float high = targetFont;
                    for (int i = 0; i < 16; ++i) {
                        const float mid = (low + high) * .5f;
                        if (fits(block.translation, mid, innerW, innerH)) low = mid; else high = mid;
                    }
                    fontSize = std::max(.01f, low);
                }

                if (!layoutDiagLogged) {
                    WeShotDiag::append(std::format(
                        L"block={} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} box={:.1f}x{:.1f} lines={} targetFont={:.2f} finalFont={:.2f}",
                        diagIndex, block.ymin, block.xmin, block.ymax, block.xmax,
                        m.pixels ? L"pixels" : L"norm1000", m.scoreNorm, m.scorePixel,
                        boxW, boxH, std::max(1, block.sourceLines), targetFont, fontSize));
                }

                auto tl = Ling::D2D::makeTextLayout(block.translation, fontSize, innerW, innerH);
                if (!tl) { ++diagIndex; continue; }
                const bool centered = block.role == L"label";
                tl->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
                tl->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (block.role == L"title" || block.role == L"heading") {
                    DWRITE_TEXT_RANGE range{ 0, (UINT32)block.translation.size() };
                    tl->SetFontWeight(block.role == L"title" ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM, range);
                }
                ctx->DrawTextLayout({ rect.left + padX, rect.top + padY }, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                ++diagIndex;
            }
            layoutDiagLogged = true;
        }
'@

# Direct screenshot overlay.
$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('#include "GeminiClient.h"')) { throw 'direct include anchor missing' }
$src = $src.Replace('#include "GeminiClient.h"', "#include \"GeminiClient.h\"`r`n#include \"WeShotDiag.h\"")
$direct = $renderer.Replace('__FUNC__','paintBlocks').Replace('__EMPTY__','blocks.empty()').Replace('__BLOCKS__','blocks').Replace('__PATH__','direct')
$pat = '(?s)        void paintBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        std::vector<BYTE> pixels;'
$patched = [regex]::Replace($src, $pat, $direct + "`r`n`r`n        std::vector<BYTE> pixels;", 1)
if ($patched -eq $src) { throw 'v0.8.17 direct renderer target not found' }
$patched = $patched.Replace('Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap;', "Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap;`r`n        bool layoutDiagLogged{ false };")
Set-Content $path $patched -Encoding utf8

# OCR/result window renderer.
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('#include "GeminiClient.h"')) {
    # Some revisions include Gemini through another wrapper; insert after pragma once.
    $src = $src.Replace('#pragma once', "#pragma once`r`n#include \"WeShotDiag.h\"")
} elseif (-not $src.Contains('#include "WeShotDiag.h"')) {
    $src = $src.Replace('#include "GeminiClient.h"', "#include \"GeminiClient.h\"`r`n#include \"WeShotDiag.h\"")
}
if (-not $src.Contains('#include "WeShotDiag.h"')) { $src = $src.Replace('#pragma once', "#pragma once`r`n#include \"WeShotDiag.h\"") }
$ocr = $renderer.Replace('__FUNC__','paintTranslationBlocks').Replace('__EMPTY__','!showTranslatedImage || translationBlocks.empty()').Replace('__BLOCKS__','translationBlocks').Replace('__PATH__','result')
$pat = '(?s)        void paintTranslationBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        void paintImage\(\)'
$patched = [regex]::Replace($src, $pat, $ocr + "`r`n`r`n        void paintImage()", 1)
if ($patched -eq $src) { throw 'v0.8.17 result renderer target not found' }
# Add once-only layout diagnostic flag near zoom state, unique anchor introduced by v0.8.12.
if ($patched.Contains('bool imageFitMode{ true };') -and -not $patched.Contains('bool layoutDiagLogged{ false };')) {
    $patched = $patched.Replace('bool imageFitMode{ true };', "bool imageFitMode{ true };`r`n        bool layoutDiagLogged{ false };")
}
Set-Content $path $patched -Encoding utf8

# Ensure a new translation result logs again after toggling/retranslation.
$src = Get-Content 'Src\WeShotOcrV2.h' -Raw
$needle = 'translationReady = true;'
if ($src.Contains($needle) -and -not $src.Contains('layoutDiagLogged = false; // v0.8.17')) {
    $src = $src.Replace($needle, "layoutDiagLogged = false; // v0.8.17`r`n            $needle")
    Set-Content 'Src\WeShotOcrV2.h' $src -Encoding utf8
}

# Stronger model instruction too; runtime inference remains the safety net.
$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw
$src = $src.Replace(
    'box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, source copied exactly from that region, translation,',
    'box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, with EVERY coordinate normalized to 0-1000 (full image is [0,0,1000,1000]; NEVER use source pixel coordinates), source copied exactly from that region, translation,')
Set-Content $path $src -Encoding utf8

# Verification.
foreach ($p in @('Src\WeShotCaptureTranslate.h','Src\WeShotOcrV2.h')) {
    $v = Get-Content $p -Raw
    foreach ($n in @('scoreN', 'scoreP', 'coord=', 'WeShotDiag::append', 'layoutDiagLogged')) {
        if (-not $v.Contains($n)) { throw "v0.8.17 verification failed in $p : $n" }
    }
}
if (-not (Test-Path 'Src\WeShotDiag.h')) { throw 'v0.8.17 diagnostic header missing' }
Write-Host 'v0.8.17 coordinate-space inference + source diagnostics applied.'
