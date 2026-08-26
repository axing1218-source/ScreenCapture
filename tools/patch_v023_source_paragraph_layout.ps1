$ErrorActionPreference = 'Stop'

# v0.8.23
# Preserve source document typography instead of trying to fill each translated area.
# Mature OCR renderers retain paragraph hierarchy, line scale and whitespace.  This
# patch reconstructs physical paragraph slots from local OCR/pixel line geometry,
# then uses the median source-line occupied height as the body typography baseline.

# -----------------------------------------------------------------------------
# 1) TranslationBlock gets one LOCAL-only metric. It is never requested from Gemini.
#    sourceLineHeight is normalized to image height (0..1000) and represents the
#    original paragraph's occupied height / physical line count.
# -----------------------------------------------------------------------------
$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('float sourceLineHeight{ 0.f };')) {
    $needle = '        int sourceLines{ 1 };'
    if (-not $src.Contains($needle)) { throw 'v0.8.23 TranslationBlock sourceLines target not found' }
    $src = $src.Replace($needle, $needle + "`r`n        float sourceLineHeight{ 0.f };")
}

# Be much stricter about visual paragraph/list segmentation.  Blank space is layout,
# not an invitation to merge regions.  Lists stay lists and preserve item line breaks.
$oldPrompt = '            L"Merge adjacent wrapped lines that belong to the same paragraph; DO NOT make a separate block for every visual line. "'
$newPrompt = '            L"Merge adjacent wrapped lines that belong to the same paragraph; DO NOT make a separate block for every visual line. "' + "`r`n" +
'            L"A visible blank vertical gap starts a NEW block: never combine text across paragraph whitespace. "' + "`r`n" +
'            L"A consecutive list/menu of short rows is ONE list-like body block, and its translation must keep one item per visible line using newline separators. "'
if ($src.Contains($oldPrompt) -and -not $src.Contains('A visible blank vertical gap starts a NEW block')) {
    $src = $src.Replace($oldPrompt, $newPrompt)
}
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Local paragraph reconstruction layer.  Reuse v0.8.21's physical line detectors
#    (Windows OCR first, visual-edge fallback), but stop matching each Gemini box to
#    arbitrary individual lines.  Build paragraph groups first, then monotonically
#    assign Gemini semantic blocks to those groups.
# -----------------------------------------------------------------------------
$paragraphHeader = @'
#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include "WeShotTextGeometry.h"
#include "GeminiClient.h"
#include "WeShotDiag.h"

namespace WeShotParagraphLayout
{
    struct Paragraph
    {
        float left{}, top{}, right{}, bottom{};
        float medianLineH{};
        int lines{};
    };

    inline float lineHeight(const WeShotTextGeometry::LineBox& l)
    {
        return std::max(1.f, l.bottom - l.top);
    }

    inline float lineWidth(const WeShotTextGeometry::LineBox& l)
    {
        return std::max(1.f, l.right - l.left);
    }

    inline float medianOf(std::vector<float> v)
    {
        if (v.empty()) return 0.f;
        std::sort(v.begin(), v.end());
        const size_t m = v.size() / 2;
        return v.size() & 1 ? v[m] : (v[m - 1] + v[m]) * .5f;
    }

    inline std::vector<Paragraph> makeParagraphs(std::vector<WeShotTextGeometry::LineBox> lines, int imageW)
    {
        std::vector<Paragraph> out;
        if (lines.empty()) return out;
        std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) {
            const float ay = (a.top + a.bottom) * .5f, by = (b.top + b.bottom) * .5f;
            if (std::fabs(ay - by) > 1.f) return ay < by;
            return a.left < b.left;
        });

        std::vector<float> allHeights;
        for (const auto& l : lines) allHeights.push_back(lineHeight(l));
        const float globalH = std::max(1.f, medianOf(allHeights));

        size_t start = 0;
        auto flush = [&](size_t endExclusive) {
            if (endExclusive <= start) return;
            Paragraph p;
            p.left = std::numeric_limits<float>::max();
            p.top = std::numeric_limits<float>::max();
            p.right = p.bottom = -1.f;
            std::vector<float> hs;
            for (size_t k = start; k < endExclusive; ++k) {
                p.left = std::min(p.left, lines[k].left);
                p.top = std::min(p.top, lines[k].top);
                p.right = std::max(p.right, lines[k].right);
                p.bottom = std::max(p.bottom, lines[k].bottom);
                hs.push_back(lineHeight(lines[k]));
            }
            p.lines = (int)(endExclusive - start);
            p.medianLineH = medianOf(hs);
            if (p.right > p.left && p.bottom > p.top) out.push_back(p);
            start = endExclusive;
        };

        for (size_t i = 1; i < lines.size(); ++i) {
            const auto& a = lines[i - 1];
            const auto& b = lines[i];
            const float ah = lineHeight(a), bh = lineHeight(b);
            const float localH = std::max(1.f, (ah + bh) * .5f);
            const float gap = b.top - a.bottom;
            const float heightRatio = std::max(ah, bh) / std::max(1.f, std::min(ah, bh));
            const float leftDelta = std::fabs(b.left - a.left);
            const float xov = std::max(0.f, std::min(a.right, b.right) - std::max(a.left, b.left));
            const float overlap = xov / std::max(1.f, std::min(lineWidth(a), lineWidth(b)));

            bool split = false;
            // Paragraph whitespace is measured relative to physical text height.
            if (gap > localH * 1.20f) split = true;
            // A font-size transition (title -> body, heading -> body) is structural even
            // when the vertical gap is modest.
            if (heightRatio > 1.27f && gap > localH * .16f) split = true;
            // Distinct columns / unrelated labels should not be collapsed into one paragraph.
            if (overlap < .08f && leftDelta > localH * 3.2f && gap > -localH * .20f) split = true;

            // Detect the end of a vertically stacked short-item list without knowing its
            // language. If several prior rows are compact and the next row suddenly spans
            // far more width after a real line gap, it is usually the following paragraph.
            const size_t groupCount = i - start;
            if (!split && groupCount >= 3) {
                std::vector<float> priorWidths;
                for (size_t k = start; k < i; ++k) priorWidths.push_back(lineWidth(lines[k]));
                const float medW = std::max(1.f, medianOf(priorWidths));
                if (lineWidth(b) > medW * 2.15f && medW < imageW * .58f && gap > globalH * .18f)
                    split = true;
            }

            if (split) flush(i);
        }
        flush(lines.size());
        return out;
    }

    inline int spanLineCount(const std::vector<Paragraph>& p, size_t a, size_t b)
    {
        int n = 0;
        for (size_t i = a; i < b; ++i) n += p[i].lines;
        return std::max(1, n);
    }

    inline float spanHeight(const std::vector<Paragraph>& p, size_t a, size_t b)
    {
        if (a >= b) return 1.f;
        return std::max(1.f, p[b - 1].bottom - p[a].top);
    }

    inline void assignSpan(GeminiClient::TranslationBlock& block,
        const std::vector<Paragraph>& p, size_t a, size_t b, int width, int height)
    {
        if (a >= b || b > p.size()) return;
        float l = std::numeric_limits<float>::max(), t = l, r = -1.f, bot = -1.f;
        int lines = 0;
        for (size_t i = a; i < b; ++i) {
            l = std::min(l, p[i].left); t = std::min(t, p[i].top);
            r = std::max(r, p[i].right); bot = std::max(bot, p[i].bottom);
            lines += p[i].lines;
        }
        if (!(r > l && bot > t) || lines <= 0) return;

        // Tiny proportional ink padding, not screenshot-size padding.
        const float occupiedPerLine = std::max(1.f, (bot - t) / (float)lines);
        const float px = occupiedPerLine * .12f;
        const float py = occupiedPerLine * .10f;
        l = std::clamp(l - px, 0.f, (float)width);
        r = std::clamp(r + px, l + 1.f, (float)width);
        t = std::clamp(t - py, 0.f, (float)height);
        bot = std::clamp(bot + py, t + 1.f, (float)height);

        block.xmin = std::clamp((int)std::lround(l * 1000.f / width), 0, 999);
        block.xmax = std::clamp((int)std::lround(r * 1000.f / width), block.xmin + 1, 1000);
        block.ymin = std::clamp((int)std::lround(t * 1000.f / height), 0, 999);
        block.ymax = std::clamp((int)std::lround(bot * 1000.f / height), block.ymin + 1, 1000);
        block.sourceLines = lines;
        block.sourceLineHeight = std::max(.01f, occupiedPerLine * 1000.f / height);
    }

    inline void apply(std::vector<GeminiClient::TranslationBlock>& blocks,
        const std::vector<BYTE>& pixels, int width, int height, const wchar_t* path)
    {
        if (blocks.empty() || pixels.empty() || width <= 0 || height <= 0) return;

        std::vector<WeShotTextGeometry::LineBox> winLines, visualLines;
        const bool winOk = WeShotTextGeometry::collectWindows(pixels, width, height, winLines) && !winLines.empty();
        const bool visualOk = WeShotTextGeometry::collectVisual(pixels, width, height, visualLines) && !visualLines.empty();

        // Prefer Windows OCR when it sees at least as much physical line structure;
        // otherwise the language-independent visual detector is a better geometry source.
        auto lines = (winOk && (!visualOk || winLines.size() >= visualLines.size())) ? winLines : visualLines;
        const wchar_t* source = (winOk && (!visualOk || winLines.size() >= visualLines.size())) ? L"windows-ocr" : L"visual-edge";
        if (lines.empty()) {
            WeShotDiag::append(std::format(L"paragraph-v023 path={} source=none blocks={} paragraphs=0 applied=0",
                path ? path : L"?", blocks.size()));
            return;
        }

        auto paragraphs = makeParagraphs(std::move(lines), width);
        if (paragraphs.empty()) return;

        // Semantic blocks are already requested from Gemini in reading order.  Geometry
        // assignment is therefore monotonic.  If counts differ, dynamic programming lets
        // one semantic block consume several physical paragraphs, guided by source_lines,
        // instead of randomly attaching it to a nearby Gemini box.
        const size_t M = blocks.size(), N = paragraphs.size();
        size_t applied = 0;

        if (M <= N && M > 0) {
            const float INF = 1e20f;
            std::vector<std::vector<float>> dp(M + 1, std::vector<float>(N + 1, INF));
            std::vector<std::vector<int>> prev(M + 1, std::vector<int>(N + 1, -1));
            dp[0][0] = 0.f;
            for (size_t i = 0; i < M; ++i) {
                for (size_t j = 0; j <= N; ++j) {
                    if (dp[i][j] >= INF / 2) continue;
                    const size_t remainingBlocks = M - i - 1;
                    const size_t maxEnd = N - remainingBlocks;
                    for (size_t e = j + 1; e <= maxEnd; ++e) {
                        const int physicalLines = spanLineCount(paragraphs, j, e);
                        const int reported = std::max(1, blocks[i].sourceLines);
                        const float lineCost = std::fabs(std::log((physicalLines + .5f) / (reported + .5f)));

                        // Titles/headings tend to occupy physically larger source lines;
                        // this helps separate title/body even if Gemini box coordinates wobble.
                        float medianH = 0.f;
                        std::vector<float> hs;
                        for (size_t q = j; q < e; ++q) hs.push_back(paragraphs[q].medianLineH);
                        medianH = medianOf(hs);
                        std::vector<float> allH;
                        for (const auto& p : paragraphs) allH.push_back(p.medianLineH);
                        const float globalMed = std::max(1.f, medianOf(allH));
                        float roleCost = 0.f;
                        if (blocks[i].role == L"title") roleCost = medianH >= globalMed * 1.12f ? -.25f : .35f;
                        else if (blocks[i].role == L"heading") roleCost = medianH >= globalMed * 1.05f ? -.12f : .18f;

                        // Strongly discourage swallowing many independent blank-gap paragraphs
                        // unless the semantic source_lines really needs them.
                        const float spanCost = (float)(e - j - 1) * .32f;
                        const float cost = dp[i][j] + lineCost * .85f + spanCost + roleCost;
                        if (cost < dp[i + 1][e]) {
                            dp[i + 1][e] = cost;
                            prev[i + 1][e] = (int)j;
                        }
                    }
                }
            }

            if (dp[M][N] < INF / 2) {
                size_t e = N;
                for (size_t ri = M; ri > 0; --ri) {
                    const int s = prev[ri][e];
                    if (s < 0) break;
                    assignSpan(blocks[ri - 1], paragraphs, (size_t)s, e, width, height);
                    ++applied;
                    e = (size_t)s;
                }
            }
        }

        // If Gemini returned more semantic blocks than physical paragraphs, preserve
        // Gemini fallback geometry rather than manufacturing overlaps.
        WeShotDiag::append(std::format(
            L"paragraph-v023 path={} source={} lines={} paragraphs={} blocks={} applied={}",
            path ? path : L"?", source, winOk ? winLines.size() : visualLines.size(),
            paragraphs.size(), blocks.size(), applied));
    }
}
'@
Set-Content 'Src\WeShotParagraphLayout.h' $paragraphHeader -Encoding utf8

# -----------------------------------------------------------------------------
# 3) Run paragraph reconstruction AFTER v0.8.21's lower-level geometry cleanup.
# -----------------------------------------------------------------------------
foreach ($path in @('Src\WeShotCaptureTranslate.h', 'Src\WeShotOcrV2.h')) {
    $src = Get-Content $path -Raw
    if (-not $src.Contains('#include "WeShotParagraphLayout.h"')) {
        $src = $src.Replace('#include "WeShotTextGeometry.h"', "#include \"WeShotTextGeometry.h\"`r`n#include \"WeShotParagraphLayout.h\"")
    }
    $src = $src.Replace('WeShotTextGeometry::stabilize(this->blocks, this->pixels, imageW, imageH, L"direct");',
        'WeShotTextGeometry::stabilize(this->blocks, this->pixels, imageW, imageH, L"direct");' + "`r`n            " +
        'WeShotParagraphLayout::apply(this->blocks, this->pixels, imageW, imageH, L"direct");')
    $src = $src.Replace('WeShotTextGeometry::stabilize(result.blocks, pixels, imageW, imageH, L"result");',
        'WeShotTextGeometry::stabilize(result.blocks, pixels, imageW, imageH, L"result");' + "`r`n            " +
        'WeShotParagraphLayout::apply(result.blocks, pixels, imageW, imageH, L"result");')
    Set-Content $path $src -Encoding utf8
}

# -----------------------------------------------------------------------------
# 4) Source-line-driven renderer.  Key rule (matching the supplied Youdao reference):
#    shorter translation DOES NOT grow to fill a big rectangle.  Physical source line
#    scale is preserved.  Only overflow causes shrinking.
# -----------------------------------------------------------------------------
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

            // The body baseline comes from physical source-line occupied height, not from
            // paragraph area.  A wide two-line paragraph can no longer inflate its font.
            std::vector<float> physicalBody;
            for (const auto& b : __BLOCKS__) {
                if (isBody(b) && b.sourceLineHeight > 0.f)
                    physicalBody.push_back(dh * b.sourceLineHeight / 1000.f);
            }
            float bodyOccupied = 0.f;
            if (!physicalBody.empty()) {
                std::sort(physicalBody.begin(), physicalBody.end());
                bodyOccupied = physicalBody[physicalBody.size() / 2];
            }

            auto fallbackFont = [&](const GeminiClient::TranslationBlock& b, float bw, float bh) {
                const auto& source = b.source.empty() ? b.translation : b.source;
                const float units = glyphUnits(source);
                const float area = std::sqrt(std::max(.01f, bw * bh) / (units * 1.18f));
                const float line = bh / (1.18f * std::max(1, b.sourceLines));
                return std::max(.01f, area * .55f + line * .45f);
            };

            struct Item {
                GeminiClient::TranslationBlock block;
                D2D1_RECT_F slot{};
                float target{};
                float font{};
                float padX{}, padY{};
            };
            std::vector<Item> items;
            items.reserve(__BLOCKS__.size());

            for (const auto& b : __BLOCKS__) {
                Item it;
                it.block = b;
                it.slot = rectFromBlock(b);
                const float bw = std::max(.5f, it.slot.right - it.slot.left);
                const float bh = std::max(.5f, it.slot.bottom - it.slot.top);

                if (b.sourceLineHeight > 0.f) {
                    float occupied = dh * b.sourceLineHeight / 1000.f;
                    if (isBody(b) && bodyOccupied > 0.f) occupied = bodyOccupied * .82f + occupied * .18f;
                    // DirectWrite em size is close to, but not identical with, visible glyph
                    // occupancy. This calibration is relative to measured source ink and is
                    // shared across all screenshot sizes.
                    it.target = occupied * .93f;
                    if (bodyOccupied > 0.f) {
                        if (b.role == L"title") it.target = std::max(it.target, bodyOccupied * 1.30f);
                        else if (b.role == L"heading") it.target = std::max(it.target, bodyOccupied * 1.14f);
                        else if (b.role == L"caption") it.target = std::min(it.target, bodyOccupied * .88f);
                    }
                }
                else {
                    it.target = fallbackFont(b, bw, bh);
                }
                it.target = std::max(.01f, it.target);
                items.push_back(std::move(it));
            }

            auto overlapArea = [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
                const float w = std::max(0.f, std::min(a.right, b.right) - std::max(a.left, b.left));
                const float h = std::max(0.f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
                return w * h;
            };

            // Local paragraph slots should already be disjoint.  Keep a deterministic
            // midpoint partition only as a fallback for unmatched Gemini geometry.
            for (size_t pass = 0; pass < 2; ++pass) {
                for (size_t i = 0; i < items.size(); ++i) for (size_t j = i + 1; j < items.size(); ++j) {
                    auto& a = items[i].slot; auto& b = items[j].slot;
                    if (overlapArea(a, b) <= .25f) continue;
                    const float acy = (a.top + a.bottom) * .5f, bcy = (b.top + b.bottom) * .5f;
                    const float acx = (a.left + a.right) * .5f, bcx = (b.left + b.right) * .5f;
                    const float xov = std::max(0.f, std::min(a.right,b.right)-std::max(a.left,b.left));
                    const float yov = std::max(0.f, std::min(a.bottom,b.bottom)-std::max(a.top,b.top));
                    if (xov >= yov) {
                        const float mid = (acy + bcy) * .5f;
                        if (acy <= bcy) { a.bottom = std::min(a.bottom, mid); b.top = std::max(b.top, mid); }
                        else { b.bottom = std::min(b.bottom, mid); a.top = std::max(a.top, mid); }
                    } else {
                        const float mid = (acx + bcx) * .5f;
                        if (acx <= bcx) { a.right = std::min(a.right, mid); b.left = std::max(b.left, mid); }
                        else { b.right = std::min(b.right, mid); a.left = std::max(a.left, mid); }
                    }
                }
            }

            auto fits = [](const std::wstring& text, float fs, float w, float h) {
                if (w <= .1f || h <= .1f || fs <= .01f) return false;
                auto tl = Ling::D2D::makeTextLayout(text, fs, w, 16384.f);
                if (!tl) return false;
                DWRITE_TEXT_METRICS m{};
                return SUCCEEDED(tl->GetMetrics(&m)) && m.height <= h + .35f && m.width <= w + .75f;
            };

            int collisions = 0, fitFailures = 0;
            for (size_t i = 0; i < items.size(); ++i)
                for (size_t j = i + 1; j < items.size(); ++j)
                    if (overlapArea(items[i].slot, items[j].slot) > .25f) ++collisions;

            for (auto& it : items) {
                const float sw = std::max(.5f, it.slot.right - it.slot.left);
                const float sh = std::max(.5f, it.slot.bottom - it.slot.top);
                it.padX = std::min(sw * .018f, it.target * .10f);
                it.padY = std::min(sh * .025f, it.target * .06f);
                const float iw = std::max(.25f, sw - it.padX * 2.f);
                const float ih = std::max(.25f, sh - it.padY * 2.f);

                // Preserve original visual size. Never enlarge a short Chinese translation
                // merely because its source paragraph rectangle is wide. Shrink only if the
                // translated text cannot fit inside the source occupied region.
                it.font = it.target;
                if (!fits(it.block.translation, it.font, iw, ih)) {
                    float lo = std::max(.01f, it.target * .08f), hi = it.target;
                    while (lo > .011f && !fits(it.block.translation, lo, iw, ih)) lo *= .5f;
                    for (int k = 0; k < 18; ++k) {
                        const float mid = (lo + hi) * .5f;
                        if (fits(it.block.translation, mid, iw, ih)) lo = mid; else hi = mid;
                    }
                    it.font = std::max(.01f, lo);
                }
                if (!fits(it.block.translation, it.font, iw, ih)) ++fitFailures;
            }

            WeShotDiag::append(std::format(
                L"layout-v023 path=__PATH__ blocks={} physical_body={:.2f} collisions={} fit_failures={}",
                items.size(), bodyOccupied, collisions, fitFailures));

            for (auto& it : items) {
                if (it.slot.right <= it.slot.left || it.slot.bottom <= it.slot.top) continue;
                auto bgColor = sampleBackground(it.block);
                const float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto textColor = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, textBrush;
                ctx->CreateSolidColorBrush(bgColor, bgBrush.GetAddressOf());
                ctx->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
                if (!bgBrush || !textBrush) continue;
                ctx->FillRectangle(it.slot, bgBrush.Get());

                const float iw = std::max(.25f, (it.slot.right-it.slot.left) - it.padX*2.f);
                const float ih = std::max(.25f, (it.slot.bottom-it.slot.top) - it.padY*2.f);
                auto tl = Ling::D2D::makeTextLayout(it.block.translation, it.font, iw, ih);
                if (!tl) continue;
                const bool centered = it.block.role == L"label";
                tl->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
                tl->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (it.block.role == L"title" || it.block.role == L"heading") {
                    DWRITE_TEXT_RANGE range{0, (UINT32)it.block.translation.size()};
                    tl->SetFontWeight(it.block.role == L"title" ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM, range);
                }
                ctx->DrawTextLayout({it.slot.left + it.padX, it.slot.top + it.padY}, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
'@

$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
$body = $renderer.Replace('__FUNC__','paintTranslationBlocks').Replace('__EMPTY__','!showTranslatedImage || translationBlocks.empty()').Replace('__BLOCKS__','translationBlocks').Replace('__PATH__','result')
$pattern = '(?s)        void paintTranslationBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        void paintImage\(\)'
$patched = [regex]::Replace($src, $pattern, $body + "`r`n`r`n        void paintImage()", 1)
if ($patched -eq $src) { throw 'v0.8.23 result renderer target not found' }
Set-Content $path $patched -Encoding utf8

$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw
$body = $renderer.Replace('__FUNC__','paintBlocks').Replace('__EMPTY__','blocks.empty()').Replace('__BLOCKS__','blocks').Replace('__PATH__','direct')
$pattern = '(?s)        void paintBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        std::vector<BYTE> pixels;'
$patched = [regex]::Replace($src, $pattern, $body + "`r`n`r`n        std::vector<BYTE> pixels;", 1)
if ($patched -eq $src) { throw 'v0.8.23 direct renderer target not found' }
Set-Content $path $patched -Encoding utf8

Write-Host 'v0.8.23 source paragraph typography layout applied.'
