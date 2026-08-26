$ErrorActionPreference = 'Stop'

# v0.8.21: Geometry must ultimately come from the pixels, not from a language model.
# Windows OCR remains the first local source when it can see words.  When it cannot
# (notably very short/thin chat captures), a language-independent horizontal-edge
# projection finds the physical text lines directly from screenshot pixels.

$helper = @'
#pragma once
#include <robuffer.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <array>
#include "GeminiClient.h"
#include "WeShotDiag.h"

namespace WeShotTextGeometry
{
    struct LineBox
    {
        float left{}, top{}, right{}, bottom{};
        float score{};
    };

    inline bool collectWindowsWorker(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        bool apartmentReady = false;
        try {
            if (pixels.empty() || width <= 0 || height <= 0) return false;
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentReady = true;
            using namespace winrt::Windows::Storage::Streams;
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;

            const auto maxDim = (int)OcrEngine::MaxImageDimension();
            if (width > maxDim || height > maxDim) {
                WeShotDiag::append(std::format(L"local-geometry windows-ocr skip=max-dimension image={}x{} max={}", width, height, maxDim));
                winrt::uninit_apartment(); apartmentReady = false;
                return false;
            }

            const uint32_t byteCount = (uint32_t)pixels.size();
            Buffer buffer(byteCount); buffer.Length(byteCount);
            auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
            BYTE* dst = nullptr;
            winrt::check_hresult(byteAccess->Buffer(&dst));
            memcpy(dst, pixels.data(), pixels.size());
            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);
            bitmap.CopyFromBuffer(buffer);
            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                WeShotDiag::append(L"local-geometry windows-ocr unavailable=no-language");
                winrt::uninit_apartment(); apartmentReady = false;
                return false;
            }

            auto result = engine.RecognizeAsync(bitmap).get();
            for (auto const& line : result.Lines()) {
                float l = std::numeric_limits<float>::max(), t = l;
                float r = -1.f, b = -1.f;
                for (auto const& word : line.Words()) {
                    auto rc = word.BoundingRect();
                    if (rc.Width <= 0.f || rc.Height <= 0.f) continue;
                    l = std::min(l, rc.X); t = std::min(t, rc.Y);
                    r = std::max(r, rc.X + rc.Width); b = std::max(b, rc.Y + rc.Height);
                }
                if (r > l && b > t) {
                    LineBox box{ l, t, r, b, (r - l) * (b - t) };
                    box.left = std::clamp(box.left, 0.f, (float)width);
                    box.right = std::clamp(box.right, box.left, (float)width);
                    box.top = std::clamp(box.top, 0.f, (float)height);
                    box.bottom = std::clamp(box.bottom, box.top, (float)height);
                    if (box.right - box.left >= 1.f && box.bottom - box.top >= 1.f) out.push_back(box);
                }
            }
            std::sort(out.begin(), out.end(), [](const LineBox& a, const LineBox& b) {
                const float ay = (a.top + a.bottom) * .5f, by = (b.top + b.bottom) * .5f;
                if (std::fabs(ay - by) > 2.f) return ay < by;
                return a.left < b.left;
            });
            WeShotDiag::append(std::format(L"local-geometry windows-ocr image={}x{} lines={}", width, height, out.size()));
            winrt::uninit_apartment(); apartmentReady = false;
            return !out.empty();
        }
        catch (...) {
            if (apartmentReady) { try { winrt::uninit_apartment(); } catch (...) {} }
            WeShotDiag::append(L"local-geometry windows-ocr failed");
            return false;
        }
    }

    inline bool collectWindows(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        bool ok = false;
        std::thread worker([&]() { ok = collectWindowsWorker(pixels, width, height, out); });
        worker.join();
        return ok;
    }

    inline int otsuThreshold(const std::array<unsigned long long, 256>& hist, unsigned long long total)
    {
        if (!total) return 0;
        long double totalSum = 0.0L;
        for (int i = 0; i < 256; ++i) totalSum += (long double)i * hist[(size_t)i];
        unsigned long long bgCount = 0;
        long double bgSum = 0.0L, best = -1.0L;
        int bestT = 0;
        for (int t = 0; t < 255; ++t) {
            bgCount += hist[(size_t)t];
            bgSum += (long double)t * hist[(size_t)t];
            if (!bgCount) continue;
            const auto fgCount = total - bgCount;
            if (!fgCount) break;
            const long double bgMean = bgSum / bgCount;
            const long double fgMean = (totalSum - bgSum) / fgCount;
            const long double d = bgMean - fgMean;
            const long double between = (long double)bgCount * fgCount * d * d;
            if (between > best) { best = between; bestT = t; }
        }
        return bestT;
    }

    inline bool collectVisual(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        if (pixels.size() < (size_t)width * height * 4 || width < 2 || height < 2) return false;

        // Horizontal colour differences are especially useful for text: vertical glyph
        // strokes generate many x-direction transitions, while flat backgrounds and
        // horizontal UI borders generate very few.  This also works in dark mode and
        // with coloured text because it uses RGB differences rather than absolute luma.
        std::vector<unsigned char> edge((size_t)width * height, 0);
        std::array<unsigned long long, 256> hist{};
        unsigned long long edgeTotal = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 1; x < width; ++x) {
                const size_t p = ((size_t)y * width + x) * 4;
                const size_t q = p - 4;
                int db = std::abs((int)pixels[p] - (int)pixels[q]);
                int dg = std::abs((int)pixels[p + 1] - (int)pixels[q + 1]);
                int dr = std::abs((int)pixels[p + 2] - (int)pixels[q + 2]);
                const int d = std::clamp(std::max({ db, dg, dr }), 0, 255);
                edge[(size_t)y * width + x] = (unsigned char)d;
                ++hist[(size_t)d]; ++edgeTotal;
            }
        }
        int edgeT = otsuThreshold(hist, edgeTotal);
        // Otsu handles image-to-image contrast.  These broad bounds merely reject
        // compression/noise at the bottom and pathological single-pixel spikes at top.
        edgeT = std::clamp(edgeT, 6, 144);

        std::vector<float> row((size_t)height, 0.f), smooth((size_t)height, 0.f);
        for (int y = 0; y < height; ++y) {
            double e = 0.0;
            for (int x = 1; x < width; ++x) {
                const int d = edge[(size_t)y * width + x];
                if (d > edgeT) e += d - edgeT;
            }
            row[(size_t)y] = (float)(e / std::max(1, width));
        }
        for (int y = 0; y < height; ++y) {
            const float a = row[(size_t)std::max(0, y - 1)];
            const float b = row[(size_t)y];
            const float c = row[(size_t)std::min(height - 1, y + 1)];
            smooth[(size_t)y] = a * .25f + b * .50f + c * .25f;
        }
        auto sorted = smooth;
        std::sort(sorted.begin(), sorted.end());
        const float median = sorted[sorted.size() / 2];
        const float peak = *std::max_element(smooth.begin(), smooth.end());
        if (!(peak > median + .01f)) return false;
        const float rowT = median + (peak - median) * .18f;

        std::vector<unsigned char> active((size_t)height, 0);
        for (int y = 0; y < height; ++y) active[(size_t)y] = smooth[(size_t)y] >= rowT;
        // Close isolated one-row holes caused by anti-aliasing or descenders.
        for (int y = 1; y + 1 < height; ++y)
            if (!active[(size_t)y] && active[(size_t)y - 1] && active[(size_t)y + 1]) active[(size_t)y] = 1;

        for (int y = 0; y < height;) {
            if (!active[(size_t)y]) { ++y; continue; }
            const int top = y;
            while (y + 1 < height && active[(size_t)y + 1]) ++y;
            const int bottom = y;

            std::vector<float> col((size_t)width, 0.f);
            double mass = 0.0;
            for (int yy = top; yy <= bottom; ++yy) {
                for (int x = 1; x < width; ++x) {
                    const int d = edge[(size_t)yy * width + x];
                    if (d > edgeT) {
                        const float v = (float)(d - edgeT);
                        col[(size_t)x] += v; mass += v;
                    }
                }
            }
            const float colPeak = *std::max_element(col.begin(), col.end());
            if (colPeak <= 0.f || mass <= 0.0) { ++y; continue; }
            const float colT = colPeak * .10f;
            int left = width, right = -1;
            for (int x = 1; x < width; ++x) if (col[(size_t)x] >= colT) {
                left = std::min(left, x - 1); right = std::max(right, x + 1);
            }
            if (right > left) {
                const float bh = (float)(bottom - top + 1);
                const float span = (float)(right - left + 1);
                // Long, fragmented edge bands are more text-like than tiny icons or
                // one straight border.  No image-size bucket is used here.
                const float score = (float)mass * (1.f + std::log1p(span / std::max(1.f, bh)));
                out.push_back({ (float)std::max(0, left), (float)top,
                    (float)std::min(width, right + 1), (float)(bottom + 1), score });
            }
            ++y;
        }

        std::sort(out.begin(), out.end(), [](const LineBox& a, const LineBox& b) {
            const float ay = (a.top + a.bottom) * .5f, by = (b.top + b.bottom) * .5f;
            if (std::fabs(ay - by) > 1.f) return ay < by;
            return a.left < b.left;
        });
        WeShotDiag::append(std::format(L"local-geometry visual-edge image={}x{} edgeT={} rowT={:.2f} lines={}",
            width, height, edgeT, rowT, out.size()));
        return !out.empty();
    }

    inline void stabilize(std::vector<GeminiClient::TranslationBlock>& blocks,
        const std::vector<BYTE>& pixels, int width, int height, const wchar_t* path)
    {
        if (blocks.empty() || pixels.empty() || width <= 0 || height <= 0) return;
        std::vector<LineBox> lines;
        bool windowsGeometry = collectWindows(pixels, width, height, lines) && !lines.empty();
        bool visualGeometry = false;
        if (!windowsGeometry) {
            lines.clear();
            visualGeometry = collectVisual(pixels, width, height, lines) && !lines.empty();
        }
        if (lines.empty()) {
            WeShotDiag::append(std::format(L"local-geometry path={} applied=0 source=none fallback=gemini", path ? path : L"?"));
            return;
        }
        const wchar_t* sourceName = windowsGeometry ? L"windows-ocr" : L"visual-edge";

        std::vector<size_t> order(blocks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const auto& x = blocks[a]; const auto& z = blocks[b];
            const int cyX = x.ymin + x.ymax, cyZ = z.ymin + z.ymax;
            if (cyX != cyZ) return cyX < cyZ;
            return x.xmin < z.xmin;
        });

        std::vector<bool> used(lines.size(), false);
        size_t applied = 0;
        for (size_t oi = 0; oi < order.size(); ++oi) {
            auto& block = blocks[order[oi]];
            int need = std::clamp(block.sourceLines, 1, 20);
            std::vector<size_t> chosen;

            if (blocks.size() == 1) {
                need = std::min(need, (int)lines.size());
                if ((int)lines.size() <= need) {
                    for (size_t i = 0; i < lines.size(); ++i) chosen.push_back(i);
                }
                else {
                    // Pick the strongest consecutive group of the requested line count.
                    // This rejects incidental borders/icons without a screenshot-size rule.
                    size_t bestStart = 0;
                    double bestScore = -1.0;
                    for (size_t s = 0; s + (size_t)need <= lines.size(); ++s) {
                        double score = 0.0;
                        for (int n = 0; n < need; ++n) score += std::max(1.f, lines[s + (size_t)n].score);
                        if (score > bestScore) { bestScore = score; bestStart = s; }
                    }
                    for (int n = 0; n < need; ++n) chosen.push_back(bestStart + (size_t)n);
                }
            }
            else {
                const float bx1 = width * block.xmin / 1000.f;
                const float bx2 = width * block.xmax / 1000.f;
                const float bcx = (bx1 + bx2) * .5f;
                const float bcy = height * (block.ymin + block.ymax) / 2000.f;
                const float bw = std::max(1.f, bx2 - bx1);
                struct Candidate { size_t index; float score; };
                std::vector<Candidate> candidates;
                for (size_t i = 0; i < lines.size(); ++i) {
                    if (used[i]) continue;
                    const auto& ln = lines[i];
                    const float lcx = (ln.left + ln.right) * .5f;
                    const float lcy = (ln.top + ln.bottom) * .5f;
                    const float overlap = std::max(0.f, std::min(ln.right, bx2) - std::max(ln.left, bx1));
                    const float overlapRatio = overlap / std::max(1.f, std::min(ln.right - ln.left, bw));
                    const float dx = std::fabs(lcx - bcx) / std::max(1, width);
                    // Y remains only a weak hint because this is exactly the axis Gemini
                    // has proven unstable on extreme aspect ratios.
                    const float dy = std::fabs(lcy - bcy) / std::max(1, height);
                    const float strength = std::log1p(std::max(0.f, ln.score));
                    const float score = dy * .35f + dx * .30f + (1.f - std::clamp(overlapRatio, 0.f, 1.f)) * 1.2f - strength * .01f;
                    candidates.push_back({ i, score });
                }
                std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
                if ((int)candidates.size() < need) continue;
                for (int n = 0; n < need; ++n) chosen.push_back(candidates[(size_t)n].index);
            }

            if (chosen.empty()) continue;
            float l = std::numeric_limits<float>::max(), t = l;
            float r = -1.f, b = -1.f;
            std::vector<float> heights;
            for (size_t li : chosen) {
                const auto& ln = lines[li];
                l = std::min(l, ln.left); t = std::min(t, ln.top);
                r = std::max(r, ln.right); b = std::max(b, ln.bottom);
                heights.push_back(std::max(1.f, ln.bottom - ln.top));
            }
            if (!(r > l && b > t)) continue;
            std::sort(heights.begin(), heights.end());
            const float medianH = heights[heights.size() / 2];

            // Edge/word boxes describe visible ink, not a typographic line box.  Expand
            // proportionally to measured glyph height so DirectWrite gets room for ascent,
            // descent and anti-aliasing.  This is scale invariant.
            const float padX = medianH * .18f;
            const float padY = medianH * .32f;
            l = std::clamp(l - padX, 0.f, (float)width);
            r = std::clamp(r + padX, l + 1.f, (float)width);
            t = std::clamp(t - padY, 0.f, (float)height);
            b = std::clamp(b + padY, t + 1.f, (float)height);

            block.xmin = std::clamp((int)std::lround(l * 1000.f / width), 0, 999);
            block.xmax = std::clamp((int)std::lround(r * 1000.f / width), block.xmin + 1, 1000);
            block.ymin = std::clamp((int)std::lround(t * 1000.f / height), 0, 999);
            block.ymax = std::clamp((int)std::lround(b * 1000.f / height), block.ymin + 1, 1000);
            block.sourceLines = std::max(1, (int)chosen.size());
            for (size_t li : chosen) used[li] = true;
            ++applied;
        }

        WeShotDiag::append(std::format(L"local-geometry path={} source={} lines={} blocks={} applied={} fallback={}",
            path ? path : L"?", sourceName, lines.size(), blocks.size(), applied,
            applied == blocks.size() ? L"none" : L"partial-gemini"));
    }
}
'@

Set-Content 'Src\WeShotTextGeometry.h' $helper -Encoding utf8
$v = Get-Content 'Src\WeShotTextGeometry.h' -Raw
foreach ($needle in @('collectVisual', 'visual-edge', 'horizontal-edge', 'sourceName', 'padY = medianH * .32f')) {
    if (-not $v.Contains($needle)) { throw "v0.8.21 verification failed: $needle" }
}
Write-Host 'v0.8.21 pixel-based visual text geometry fallback applied.'
