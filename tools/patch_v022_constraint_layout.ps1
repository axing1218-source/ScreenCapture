$ErrorActionPreference = 'Stop'

# v0.8.22
# Mature OCR/layout-style rendering:
# 1) recover paragraph-like regions instead of painting every line independently;
# 2) treat each original region as a hard layout slot;
# 3) preserve source visual size when it fits;
# 4) only shrink translation when it cannot fit its original slot;
# 5) globally partition overlapping slots so later blocks can never paint over earlier text;
# 6) run a deterministic fit/collision audit before drawing.

$renderer = @'
        void __FUNC__(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            if (!ctx || __EMPTY__) return;
            const float dw = imageRect.right - imageRect.left;
            const float dh = imageRect.bottom - imageRect.top;
            if (dw <= .5f || dh <= .5f) return;

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

            auto isBody = [](const GeminiClient::TranslationBlock& b) {
                return b.role.empty() || b.role == L"body";
            };

            auto rectFromBlock = [&](const GeminiClient::TranslationBlock& b) {
                return D2D1::RectF(
                    imageRect.left + dw * b.xmin / 1000.f,
                    imageRect.top + dh * b.ymin / 1000.f,
                    imageRect.left + dw * b.xmax / 1000.f,
                    imageRect.top + dh * b.ymax / 1000.f);
            };

            auto estimateSourceFont = [&](const GeminiClient::TranslationBlock& block, float boxW, float boxH) {
                const auto& sourceText = block.source.empty() ? block.translation : block.source;
                const float units = glyphUnits(sourceText);
                const int lines = std::max(1, block.sourceLines);
                const float lineFont = boxH / (1.18f * (float)lines);
                const float areaFont = std::sqrt(std::max(.01f, boxW * boxH) / (units * 1.12f));
                // Once local geometry is available, line height is the strongest signal;
                // area/content density stabilizes unusually wide or narrow text regions.
                float f = lineFont * .62f + areaFont * .38f;
                return std::max(.01f, f);
            };

            auto xOverlapRatio = [](const GeminiClient::TranslationBlock& a,
                const GeminiClient::TranslationBlock& b) {
                const int overlap = std::max(0, std::min(a.xmax, b.xmax) - std::max(a.xmin, b.xmin));
                const int denom = std::max(1, std::min(a.xmax - a.xmin, b.xmax - b.xmin));
                return (float)overlap / denom;
            };

            // -----------------------------------------------------------------
            // Paragraph recovery. Mature OCR engines expose paragraph->line->word
            // hierarchy. Gemini may occasionally return adjacent body lines as separate
            // blocks, so recover the paragraph relationship from relative geometry.
            // All thresholds are ratios of measured line height/overlap, never screenshot
            // pixel-size buckets.
            // -----------------------------------------------------------------
            std::vector<GeminiClient::TranslationBlock> ordered = __BLOCKS__;
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
                if (a.ymin != b.ymin) return a.ymin < b.ymin;
                return a.xmin < b.xmin;
            });

            std::vector<GeminiClient::TranslationBlock> blocks;
            blocks.reserve(ordered.size());
            for (auto block : ordered) {
                if (!blocks.empty()) {
                    auto& prev = blocks.back();
                    const auto rp = rectFromBlock(prev);
                    const auto rc = rectFromBlock(block);
                    const float prevLine = std::max(.5f, (rp.bottom - rp.top) / std::max(1, prev.sourceLines));
                    const float curLine = std::max(.5f, (rc.bottom - rc.top) / std::max(1, block.sourceLines));
                    const float lineH = (prevLine + curLine) * .5f;
                    const float gap = rc.top - rp.bottom;
                    const float leftDelta = std::fabs(rc.left - rp.left);
                    const float overlap = xOverlapRatio(prev, block);
                    const bool sameParagraph = isBody(prev) && isBody(block)
                        && overlap >= .55f
                        && leftDelta <= lineH * 1.25f
                        && gap >= -lineH * .30f
                        && gap <= lineH * .95f;
                    if (sameParagraph) {
                        prev.ymin = std::min(prev.ymin, block.ymin);
                        prev.xmin = std::min(prev.xmin, block.xmin);
                        prev.ymax = std::max(prev.ymax, block.ymax);
                        prev.xmax = std::max(prev.xmax, block.xmax);
                        if (!block.source.empty()) {
                            if (!prev.source.empty()) prev.source += L"\n";
                            prev.source += block.source;
                        }
                        if (!block.translation.empty()) {
                            if (!prev.translation.empty()) prev.translation += L"\n";
                            prev.translation += block.translation;
                        }
                        prev.sourceLines = std::max(1, prev.sourceLines) + std::max(1, block.sourceLines);
                        continue;
                    }
                }
                blocks.push_back(std::move(block));
            }

            struct Item {
                GeminiClient::TranslationBlock block;
                D2D1_RECT_F base{};
                D2D1_RECT_F slot{};
                float targetFont{};
                float fontSize{};
                float padX{};
                float padY{};
            };
            std::vector<Item> items;
            items.reserve(blocks.size());

            std::vector<float> bodyFonts;
            for (const auto& b : blocks) {
                const auto r = rectFromBlock(b);
                const float bw = std::max(.5f, r.right - r.left);
                const float bh = std::max(.5f, r.bottom - r.top);
                if (isBody(b)) bodyFonts.push_back(estimateSourceFont(b, bw, bh));
            }
            float bodyFont = 0.f;
            if (!bodyFonts.empty()) {
                std::sort(bodyFonts.begin(), bodyFonts.end());
                bodyFont = bodyFonts[bodyFonts.size() / 2];
            }

            for (const auto& b : blocks) {
                Item it;
                it.block = b;
                it.base = rectFromBlock(b);
                it.slot = it.base;
                const float bw = std::max(.5f, it.base.right - it.base.left);
                const float bh = std::max(.5f, it.base.bottom - it.base.top);
                it.targetFont = estimateSourceFont(b, bw, bh);
                if (bodyFont > 0.f) {
                    if (isBody(b)) it.targetFont = bodyFont * .65f + it.targetFont * .35f;
                    else if (b.role == L"title") it.targetFont = std::max(it.targetFont, bodyFont * 1.30f);
                    else if (b.role == L"heading") it.targetFont = std::max(it.targetFont, bodyFont * 1.15f);
                    else if (b.role == L"caption") it.targetFont = std::min(it.targetFont, bodyFont * .92f);
                }
                items.push_back(std::move(it));
            }

            auto overlapArea = [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
                const float w = std::max(0.f, std::min(a.right, b.right) - std::max(a.left, b.left));
                const float h = std::max(0.f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
                return w * h;
            };

            int collisionsBefore = 0;
            for (size_t i = 0; i < items.size(); ++i)
                for (size_t j = i + 1; j < items.size(); ++j)
                    if (overlapArea(items[i].slot, items[j].slot) > .25f) ++collisionsBefore;

            // -----------------------------------------------------------------
            // Global no-overlap constraint. If upstream geometry overlaps, split the
            // shared space at the midpoint between region centres. This is deterministic:
            // later drawing can no longer erase an earlier translated sentence.
            // -----------------------------------------------------------------
            for (size_t pass = 0; pass < 3; ++pass) {
                for (size_t i = 0; i < items.size(); ++i) {
                    for (size_t j = i + 1; j < items.size(); ++j) {
                        auto& a = items[i].slot;
                        auto& b = items[j].slot;
                        const float aw = std::max(.5f, a.right - a.left);
                        const float bw = std::max(.5f, b.right - b.left);
                        const float ah = std::max(.5f, a.bottom - a.top);
                        const float bh = std::max(.5f, b.bottom - b.top);
                        const float xov = std::max(0.f, std::min(a.right, b.right) - std::max(a.left, b.left));
                        const float yov = std::max(0.f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
                        if (xov <= 0.f || yov <= 0.f) continue;
                        const float xr = xov / std::max(.5f, std::min(aw, bw));
                        const float yr = yov / std::max(.5f, std::min(ah, bh));
                        const float acx = (a.left + a.right) * .5f, bcx = (b.left + b.right) * .5f;
                        const float acy = (a.top + a.bottom) * .5f, bcy = (b.top + b.bottom) * .5f;

                        if (xr >= yr) {
                            const float boundary = (acy + bcy) * .5f;
                            if (acy <= bcy) { a.bottom = std::min(a.bottom, boundary); b.top = std::max(b.top, boundary); }
                            else { b.bottom = std::min(b.bottom, boundary); a.top = std::max(a.top, boundary); }
                        }
                        else {
                            const float boundary = (acx + bcx) * .5f;
                            if (acx <= bcx) { a.right = std::min(a.right, boundary); b.left = std::max(b.left, boundary); }
                            else { b.right = std::min(b.right, boundary); a.left = std::max(a.left, boundary); }
                        }
                    }
                }
            }

            // Never leave an invalid slot. Fall back to a tiny valid centre slice; the
            // fit pass below will shrink text rather than allowing it to invade a neighbour.
            for (auto& it : items) {
                if (it.slot.right <= it.slot.left + .5f) {
                    const float c = (it.base.left + it.base.right) * .5f;
                    it.slot.left = c - .25f; it.slot.right = c + .25f;
                }
                if (it.slot.bottom <= it.slot.top + .5f) {
                    const float c = (it.base.top + it.base.bottom) * .5f;
                    it.slot.top = c - .25f; it.slot.bottom = c + .25f;
                }
                it.slot.left = std::max(imageRect.left, it.slot.left);
                it.slot.top = std::max(imageRect.top, it.slot.top);
                it.slot.right = std::min(imageRect.right, it.slot.right);
                it.slot.bottom = std::min(imageRect.bottom, it.slot.bottom);
            }

            auto measureFits = [](const std::wstring& text, float fs, float width, float height,
                DWRITE_TEXT_METRICS* outMetrics = nullptr) {
                if (width <= .1f || height <= .1f || fs <= .01f) return false;
                auto layout = Ling::D2D::makeTextLayout(text, fs, width, 16384.f);
                if (!layout) return false;
                DWRITE_TEXT_METRICS metrics{};
                if (FAILED(layout->GetMetrics(&metrics))) return false;
                if (outMetrics) *outMetrics = metrics;
                return metrics.height <= height + .35f && metrics.width <= width + .75f;
            };

            int fitFailures = 0;
            for (auto& it : items) {
                const float sw = std::max(.5f, it.slot.right - it.slot.left);
                const float sh = std::max(.5f, it.slot.bottom - it.slot.top);
                it.padX = std::min(sw * .025f, it.targetFont * .14f);
                it.padY = std::min(sh * .045f, it.targetFont * .08f);
                const float innerW = std::max(.25f, sw - it.padX * 2.f);
                const float innerH = std::max(.25f, sh - it.padY * 2.f);

                // User-facing rule: preserve the original visual size if it fits in the
                // original occupied region. Only then shrink, using binary search for the
                // largest size that fits exactly inside that slot.
                it.fontSize = it.targetFont;
                if (!measureFits(it.block.translation, it.fontSize, innerW, innerH)) {
                    float low = std::max(.01f, it.targetFont * .08f);
                    while (low > .011f && !measureFits(it.block.translation, low, innerW, innerH)) low *= .5f;
                    float high = it.targetFont;
                    for (int k = 0; k < 18; ++k) {
                        const float mid = (low + high) * .5f;
                        if (measureFits(it.block.translation, mid, innerW, innerH)) low = mid;
                        else high = mid;
                    }
                    it.fontSize = std::max(.01f, low);
                }
                if (!measureFits(it.block.translation, it.fontSize, innerW, innerH)) ++fitFailures;
            }

            int collisionsAfter = 0;
            for (size_t i = 0; i < items.size(); ++i)
                for (size_t j = i + 1; j < items.size(); ++j)
                    if (overlapArea(items[i].slot, items[j].slot) > .25f) ++collisionsAfter;

            WeShotDiag::append(std::format(
                L"layout-v022 path=__PATH__ input={} paragraphs={} collisions_before={} collisions_after={} fit_failures={}",
                __BLOCKS__.size(), items.size(), collisionsBefore, collisionsAfter, fitFailures));

            // Draw only after the full layout has passed through paragraph recovery,
            // no-overlap constraints and text fitting. Draw order can no longer change
            // the geometry chosen for another block.
            for (auto& it : items) {
                auto bgColor = sampleBackground(it.block);
                const float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto textColor = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, textBrush;
                ctx->CreateSolidColorBrush(bgColor, bgBrush.GetAddressOf());
                ctx->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
                if (!bgBrush || !textBrush) continue;
                ctx->FillRectangle(it.slot, bgBrush.Get());

                const float innerW = std::max(.25f, (it.slot.right - it.slot.left) - it.padX * 2.f);
                const float innerH = std::max(.25f, (it.slot.bottom - it.slot.top) - it.padY * 2.f);
                auto tl = Ling::D2D::makeTextLayout(it.block.translation, it.fontSize, innerW, innerH);
                if (!tl) continue;
                const bool centered = it.block.role == L"label";
                tl->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
                tl->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (it.block.role == L"title" || it.block.role == L"heading") {
                    DWRITE_TEXT_RANGE range{ 0, (UINT32)it.block.translation.size() };
                    tl->SetFontWeight(it.block.role == L"title" ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM, range);
                }
                ctx->DrawTextLayout({ it.slot.left + it.padX, it.slot.top + it.padY },
                    tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
'@

# OCR/result window renderer.
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
$ocr = $renderer.Replace('__FUNC__', 'paintTranslationBlocks')
$ocr = $ocr.Replace('__EMPTY__', '!showTranslatedImage || translationBlocks.empty()')
$ocr = $ocr.Replace('__BLOCKS__', 'translationBlocks')
$ocr = $ocr.Replace('__PATH__', 'result')
$pattern = '(?s)        void paintTranslationBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        void paintImage\(\)'
$patched = [regex]::Replace($src, $pattern, $ocr + "`r`n`r`n        void paintImage()", 1)
if ($patched -eq $src) { throw 'v0.8.22 target not found: result renderer' }
Set-Content $path $patched -Encoding utf8

# Direct screenshot renderer.
$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw
$direct = $renderer.Replace('__FUNC__', 'paintBlocks')
$direct = $direct.Replace('__EMPTY__', 'blocks.empty()')
$direct = $direct.Replace('__BLOCKS__', 'blocks')
$direct = $direct.Replace('__PATH__', 'direct')
$pattern = '(?s)        void paintBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        std::vector<BYTE> pixels;'
$patched = [regex]::Replace($src, $pattern, $direct + "`r`n`r`n        std::vector<BYTE> pixels;", 1)
if ($patched -eq $src) { throw 'v0.8.22 target not found: direct renderer' }
Set-Content $path $patched -Encoding utf8

foreach ($p in @('Src\WeShotOcrV2.h','Src\WeShotCaptureTranslate.h')) {
    $v = Get-Content $p -Raw
    foreach ($needle in @('layout-v022', 'sameParagraph', 'collisionsAfter', 'measureFits', 'targetFont * .08f')) {
        if (-not $v.Contains($needle)) { throw "v0.8.22 verification failed in $p: $needle" }
    }
}
Write-Host 'v0.8.22 paragraph constraint layout applied.'
