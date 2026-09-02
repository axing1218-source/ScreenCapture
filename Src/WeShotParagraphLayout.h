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
