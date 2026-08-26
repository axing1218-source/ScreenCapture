$ErrorActionPreference = 'Stop'
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw

function Replace-Checked([string]$old, [string]$new, [string]$label) {
    if (-not $script:src.Contains($old)) { throw "Patch target not found: $label" }
    $script:src = $script:src.Replace($old, $new)
}

# 1) Translation result: leave loading state cleanly and restore source text on error.
Replace-Checked @'
            translating = false;
            if (!status || !textBox) return;
            if (!result.ok) {
                status->setText(result.error.empty() ? L"Gemini 翻译失败。" : result.error);
                if (translateBtn) translateBtn->setText(L"翻译");
                return;
            }
'@ @'
            translating = false;
            if (!status || !textBox) return;
            textBox->setBg(0xFAFAFAFF);
            if (!result.ok) {
                status->setText(result.error.empty() ? L"Gemini 翻译失败。" : result.error);
                textBox->setText(originalText);
                if (translateBtn) translateBtn->setText(L"翻译");
                refresh();
                return;
            }
'@ 'translation result state'

# 2) Always restore the normal result box when switching between original/translation.
Replace-Checked @'
            if (textBox) textBox->setText(translated ? translatedText : originalText);
'@ @'
            if (textBox) {
                textBox->setBg(0xFAFAFAFF);
                textBox->setText(translated ? translatedText : originalText);
            }
'@ 'showMode text box'

# 3) Make translated text fit very short screenshots instead of forcing a 7px minimum.
Replace-Checked @'
                float boxH = rect.bottom - rect.top;
                float fontSize = std::clamp(boxH * .62f, 7.f * dpi, 24.f * dpi);
                auto tl = Ling::D2D::makeTextLayout(block.translation, fontSize,
                    std::max(1.f, rect.right - rect.left), std::max(1.f, boxH));
                if (!tl) continue;
                tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                ctx->DrawTextLayout({ rect.left, rect.top }, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
'@ @'
                float boxW = rect.right - rect.left;
                float boxH = rect.bottom - rect.top;

                // A single-line/very short screenshot benefits from using almost all of its vertical space.
                if (translationBlocks.size() == 1 && dh < 140.f * dpi) {
                    const float edge = std::min(2.f * dpi, dh * .05f);
                    rect.top = imageRect.top + edge;
                    rect.bottom = imageRect.bottom - edge;
                    boxH = std::max(1.f, rect.bottom - rect.top);
                }

                const float padX = std::min(2.f * dpi, boxW * .05f);
                const float padY = std::min(1.f * dpi, boxH * .04f);
                const float innerW = std::max(1.f, boxW - padX * 2.f);
                const float innerH = std::max(1.f, boxH - padY * 2.f);

                // Pick the largest font that actually fits after wrapping. This is much more stable
                // than deriving the font only from box height, especially on low-height captures.
                float maxFont = std::min(24.f * dpi, std::max(2.75f * dpi, boxH * .68f));
                float minFont = std::max(2.75f * dpi, std::min(maxFont, boxH * .16f));
                float fontSize = maxFont;
                bool fitted = false;
                const float step = std::max(.25f, .5f * dpi);
                for (float fs = maxFont; fs >= minFont; fs -= step) {
                    auto probe = Ling::D2D::makeTextLayout(block.translation, fs, innerW, 4096.f * dpi);
                    if (!probe) continue;
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(probe->GetMetrics(&metrics)) && metrics.height <= innerH + .5f) {
                        fontSize = fs;
                        fitted = true;
                        break;
                    }
                }
                if (!fitted) fontSize = minFont;

                auto tl = Ling::D2D::makeTextLayout(block.translation, fontSize, innerW, innerH);
                if (!tl) continue;
                tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                ctx->DrawTextLayout({ rect.left + padX, rect.top + padY }, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
'@ 'adaptive translated text'

# 4) Show an obvious translucent loading state over the screenshot while Gemini is translating.
Replace-Checked @'
                ctx->DrawBitmap(imageBitmap.Get(), dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                paintTranslationBlocks(ctx, dest);
'@ @'
                ctx->DrawBitmap(imageBitmap.Get(), dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                if (translating) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBrush, loadingTextBrush;
                    ctx->CreateSolidColorBrush(D2D1::ColorF(.38f, .38f, .38f, .46f), overlayBrush.GetAddressOf());
                    ctx->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), loadingTextBrush.GetAddressOf());
                    if (overlayBrush) ctx->FillRectangle(dest, overlayBrush.Get());
                    if (loadingTextBrush) {
                        const float loadingFont = std::clamp(16.f * dpi, 12.f * dpi, 22.f * dpi);
                        auto loadingLayout = Ling::D2D::makeTextLayout(L"正在翻译中...", loadingFont,
                            std::max(1.f, dw), std::max(1.f, dh));
                        if (loadingLayout) {
                            loadingLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            loadingLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                            ctx->DrawTextLayout({ dest.left, dest.top }, loadingLayout.Get(), loadingTextBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        }
                    }
                }
                else {
                    paintTranslationBlocks(ctx, dest);
                }
'@ 'image loading overlay'

# 5) Gray/translucent result box and immediate loading message after the user clicks Translate.
Replace-Checked @'
            translating = true;
            if (translateBtn) translateBtn->setText(L"翻译中...");
            if (status) status->setText(geminiOcrBlocks.empty()
'@ @'
            translating = true;
            if (translateBtn) translateBtn->setText(L"翻译中...");
            if (textBox) {
                textBox->setBg(0xE2E2E2CC);
                textBox->setText(L"正在翻译中...");
            }
            refresh();
            if (status) status->setText(geminiOcrBlocks.empty()
'@ 'translation loading state'

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @(
    'textBox->setBg(0xE2E2E2CC);',
    'L"正在翻译中..."',
    'DWRITE_TEXT_METRICS metrics{};',
    'translationBlocks.size() == 1 && dh < 140.f * dpi',
    'D2D1::ColorF(.38f, .38f, .38f, .46f)'
)) {
    if (-not $verify.Contains($needle)) { throw "Verification failed: $needle" }
}
Write-Host 'Translation UX patch applied successfully.'

# v0.8.10: zoom controls + long screenshot direct translation.
& (Join-Path $PSScriptRoot 'patch_v010_zoom_long.ps1')
& (Join-Path $PSScriptRoot 'patch_v010_compilefix.ps1')
# v0.8.11: complete long screenshot translation + functional Original tab.
& (Join-Path $PSScriptRoot 'patch_v011_long_translation_fix.ps1')
# v0.8.12: region-based Youdao-style image layout + mouse-wheel zoom.
& (Join-Path $PSScriptRoot 'patch_v012_youdao_layout.ps1')