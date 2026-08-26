$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# v0.8.16: continuous adaptive typography.
# No screenshot-height buckets or short/long special cases are used.
# Font size is inferred from the source region geometry + source text density,
# then only reduced when the translated text really cannot fit its region.
# -----------------------------------------------------------------------------

# 1) Give each translated visual region its source text as well.  This lets the
# renderer estimate the source's visual font size from actual content density
# instead of trusting source_lines alone.
$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring translation;
        std::wstring role{ L"body" };
        int sourceLines{ 1 };
    };
'@ @'
    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring source;
        std::wstring translation;
        std::wstring role{ L"body" };
        int sourceLines{ 1 };
    };
'@ 'translation block source text'

Replace-Checked ([ref]$src) @'
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"translation", strType);
        blockProps.SetNamedValue(L"role", strType);
        blockProps.SetNamedValue(L"source_lines", intType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
        blockReq.Append(JsonValue::CreateStringValue(L"role"));
        blockReq.Append(JsonValue::CreateStringValue(L"source_lines"));
'@ @'
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"source", strType);
        blockProps.SetNamedValue(L"translation", strType);
        blockProps.SetNamedValue(L"role", strType);
        blockProps.SetNamedValue(L"source_lines", intType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"source"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
        blockReq.Append(JsonValue::CreateStringValue(L"role"));
        blockReq.Append(JsonValue::CreateStringValue(L"source_lines"));
'@ 'translation block schema source'

Replace-Checked ([ref]$src) @'
            L"Each block must contain box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, translation, "
            L"role as exactly one of title, heading, body, caption, label, and source_lines as the number of visible source lines in that region. "
'@ @'
            L"Each block must contain box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, source copied exactly from that region, translation, "
            L"role as exactly one of title, heading, body, caption, label, and source_lines as the number of visible source lines in that region. "
            L"Preserve visible line breaks inside each block's source string whenever possible. "
'@ 'translation prompt source region text'

Replace-Checked ([ref]$src) @'
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                auto role = std::wstring{ item.GetNamedString(L"role", L"body") };
                int sourceLines = (int)std::lround(item.GetNamedNumber(L"source_lines", 1));
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
'@ @'
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto source = std::wstring{ item.GetNamedString(L"source", L"") };
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                auto role = std::wstring{ item.GetNamedString(L"role", L"body") };
                int sourceLines = (int)std::lround(item.GetNamedNumber(L"source_lines", 1));
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.source = std::move(source);
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
'@ 'parse source text per translation region'

Replace-Checked ([ref]$src) @'
                b.ymin = sourceBlocks[id].ymin; b.xmin = sourceBlocks[id].xmin;
                b.ymax = sourceBlocks[id].ymax; b.xmax = sourceBlocks[id].xmax;
                b.translation = std::move(tr);
                b.role = L"body";
'@ @'
                b.ymin = sourceBlocks[id].ymin; b.xmin = sourceBlocks[id].xmin;
                b.ymax = sourceBlocks[id].ymax; b.xmax = sourceBlocks[id].xmax;
                b.source = sourceBlocks[id].source;
                b.translation = std::move(tr);
                b.role = L"body";
'@ 'fallback source text per translation region'

Set-Content $path $src -Encoding utf8

# Shared renderer body.  The same mathematical model is injected into both the
# direct-capture overlay and the OCR/result window.
$renderer = @'
        void __FUNC__(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            if (!ctx || __EMPTY__) return;
            const float dw = imageRect.right - imageRect.left;
            const float dh = imageRect.bottom - imageRect.top;

            // Approximate source glyph advance in em units.  This is content-based,
            // not screenshot-size based: CJK glyphs are roughly square, Latin letters
            // are narrower, and spaces/punctuation are narrower again.
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

            auto estimateSourceFont = [&](const GeminiClient::TranslationBlock& block, float boxW, float boxH) {
                const auto& sourceText = block.source.empty() ? block.translation : block.source;
                const float units = glyphUnits(sourceText);

                // If text of total advance U occupies a region W x H, its font scale
                // is approximately sqrt(W*H/U).  This remains stable for one line,
                // multiple lines, tiny captures, large captures and zoomed long images.
                const float areaFont = std::sqrt(std::max(.01f, boxW * boxH) / (units * 1.18f));

                // source_lines is still useful, but never allowed to collapse the estimate
                // when the model reports an implausible line count.  Clamp it relative to
                // the geometry/content estimate and blend rather than treating it as law.
                const int reportedLines = std::max(1, block.sourceLines);
                float lineFont = boxH / (1.18f * (float)reportedLines);
                lineFont = std::clamp(lineFont, areaFont * .62f, areaFont * 1.55f);
                return std::max(.01f, areaFont * .72f + lineFont * .28f);
            };

            // Stabilize normal body text with a median source-font baseline.  Unlike the
            // previous median line-height method, each candidate is already inferred from
            // both region area and source content, so a bad source_lines value cannot make
            // a one-line sentence collapse to a dot.
            std::vector<float> bodyFonts;
            bodyFonts.reserve(__BLOCKS__.size());
            for (const auto& block : __BLOCKS__) {
                const float boxW = dw * std::max(1, block.xmax - block.xmin) / 1000.f;
                const float boxH = dh * std::max(1, block.ymax - block.ymin) / 1000.f;
                if (boxW < .5f || boxH < .5f) continue;
                if (block.role.empty() || block.role == L"body") {
                    bodyFonts.push_back(estimateSourceFont(block, boxW, boxH));
                }
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

            for (const auto& block : __BLOCKS__) {
                D2D1_RECT_F rect{
                    imageRect.left + dw * block.xmin / 1000.f,
                    imageRect.top + dh * block.ymin / 1000.f,
                    imageRect.left + dw * block.xmax / 1000.f,
                    imageRect.top + dh * block.ymax / 1000.f
                };
                const float boxW = rect.right - rect.left;
                const float boxH = rect.bottom - rect.top;
                if (boxW < .5f || boxH < .5f) continue;

                auto bgColor = sampleBackground(block);
                const float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto textColor = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, textBrush;
                ctx->CreateSolidColorBrush(bgColor, bgBrush.GetAddressOf());
                ctx->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
                if (!bgBrush || !textBrush) continue;
                ctx->FillRectangle(rect, bgBrush.Get());

                float targetFont = estimateSourceFont(block, boxW, boxH);
                if (bodyFont > 0.f) {
                    if (block.role.empty() || block.role == L"body") {
                        targetFont = bodyFont * .72f + targetFont * .28f;
                    }
                    else if (block.role == L"title") {
                        targetFont = std::max(targetFont, bodyFont * 1.30f);
                    }
                    else if (block.role == L"heading") {
                        targetFont = std::max(targetFont, bodyFont * 1.15f);
                    }
                    else if (block.role == L"caption") {
                        targetFont = std::min(targetFont, bodyFont * .92f);
                    }
                }

                // Padding is proportional to the source font, not to a screenshot-height
                // category.  It therefore scales naturally with tiny captures and zoom.
                const float padX = std::min(boxW * .035f, targetFont * .18f);
                const float padY = std::min(boxH * .06f, targetFont * .10f);
                const float innerW = std::max(.5f, boxW - padX * 2.f);
                const float innerH = std::max(.5f, boxH - padY * 2.f);

                // Keep the source visual size whenever it fits.  Only if the translation
                // needs more room do a binary search for the largest fitting size.
                float fontSize = targetFont;
                if (!fits(block.translation, fontSize, innerW, innerH)) {
                    float low = targetFont * .18f;
                    while (low > .01f && !fits(block.translation, low, innerW, innerH)) low *= .5f;
                    float high = targetFont;
                    for (int i = 0; i < 16; ++i) {
                        const float mid = (low + high) * .5f;
                        if (fits(block.translation, mid, innerW, innerH)) low = mid;
                        else high = mid;
                    }
                    fontSize = std::max(.01f, low);
                }

                auto tl = Ling::D2D::makeTextLayout(block.translation, fontSize, innerW, innerH);
                if (!tl) continue;
                const bool centered = block.role == L"label";
                tl->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
                tl->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (block.role == L"title" || block.role == L"heading") {
                    DWRITE_TEXT_RANGE range{ 0, (UINT32)block.translation.size() };
                    tl->SetFontWeight(block.role == L"title" ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM, range);
                }
                ctx->DrawTextLayout({ rect.left + padX, rect.top + padY }, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
'@

# 2) OCR / result-window renderer.
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
$ocrRenderer = $renderer.Replace('__FUNC__', 'paintTranslationBlocks').Replace('__EMPTY__', '!showTranslatedImage || translationBlocks.empty()').Replace('__BLOCKS__', 'translationBlocks')
$pattern = '(?s)        void paintTranslationBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        void paintImage\(\)'
$patched = [regex]::Replace($src, $pattern, $ocrRenderer + "`r`n`r`n        void paintImage()", 1)
if ($patched -eq $src) { throw 'v0.8.16 target not found: OCR/result translation renderer' }
Set-Content $path $patched -Encoding utf8

# 3) Direct normal-screenshot renderer.
$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw
$directRenderer = $renderer.Replace('__FUNC__', 'paintBlocks').Replace('__EMPTY__', 'blocks.empty()').Replace('__BLOCKS__', 'blocks')
$pattern = '(?s)        void paintBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        std::vector<BYTE> pixels;'
$patched = [regex]::Replace($src, $pattern, $directRenderer + "`r`n`r`n        std::vector<BYTE> pixels;", 1)
if ($patched -eq $src) { throw 'v0.8.16 target not found: direct screenshot translation renderer' }
Set-Content $path $patched -Encoding utf8

# Verification: both paths must use the same continuous estimator and binary-fit logic.
foreach ($p in @('Src\WeShotOcrV2.h', 'Src\WeShotCaptureTranslate.h')) {
    $v = Get-Content $p -Raw
    foreach ($needle in @('glyphUnits', 'areaFont', 'targetFont * .18f', 'for (int i = 0; i < 16; ++i)')) {
        if (-not $v.Contains($needle)) { throw "v0.8.16 verification failed in $p : $needle" }
    }
}
$v = Get-Content 'Src\GeminiClient.h' -Raw
if (-not $v.Contains('std::wstring source;') -or -not $v.Contains('L"source"')) {
    throw 'v0.8.16 verification failed: translation source metadata'
}
Write-Host 'v0.8.16 continuous adaptive translation typography applied successfully.'
