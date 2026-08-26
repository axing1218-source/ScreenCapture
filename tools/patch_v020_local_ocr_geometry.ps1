$ErrorActionPreference = 'Stop'

# v0.8.20
# Youdao-style separation of concerns:
# - Windows OCR provides physical word/line geometry from image pixels.
# - Gemini remains responsible for semantic OCR/translation.
# - Gemini boxes are used only as matching hints and as a fallback.
# No extra cloud OCR provider or external runtime dependency is added.

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
#include "GeminiClient.h"
#include "WeShotDiag.h"

namespace WeShotTextGeometry
{
    struct LineBox
    {
        float left{}, top{}, right{}, bottom{};
    };

    inline bool collectWorker(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
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
                WeShotDiag::append(std::format(L"local-geometry skip=max-dimension image={}x{} max={}", width, height, maxDim));
                winrt::uninit_apartment();
                apartmentReady = false;
                return false;
            }

            const uint32_t byteCount = (uint32_t)pixels.size();
            Buffer buffer(byteCount);
            buffer.Length(byteCount);
            auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
            BYTE* dst = nullptr;
            winrt::check_hresult(byteAccess->Buffer(&dst));
            memcpy(dst, pixels.data(), pixels.size());

            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);
            bitmap.CopyFromBuffer(buffer);
            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                WeShotDiag::append(L"local-geometry unavailable=no-windows-ocr-language");
                winrt::uninit_apartment();
                apartmentReady = false;
                return false;
            }

            auto result = engine.RecognizeAsync(bitmap).get();
            for (auto const& line : result.Lines()) {
                float l = std::numeric_limits<float>::max();
                float t = std::numeric_limits<float>::max();
                float r = -1.f, b = -1.f;
                for (auto const& word : line.Words()) {
                    auto rc = word.BoundingRect();
                    if (rc.Width <= 0.f || rc.Height <= 0.f) continue;
                    l = std::min(l, rc.X);
                    t = std::min(t, rc.Y);
                    r = std::max(r, rc.X + rc.Width);
                    b = std::max(b, rc.Y + rc.Height);
                }
                if (r > l && b > t) {
                    LineBox box{ l, t, r, b };
                    box.left = std::clamp(box.left, 0.f, (float)width);
                    box.right = std::clamp(box.right, box.left, (float)width);
                    box.top = std::clamp(box.top, 0.f, (float)height);
                    box.bottom = std::clamp(box.bottom, box.top, (float)height);
                    if (box.right - box.left >= 1.f && box.bottom - box.top >= 1.f) out.push_back(box);
                }
            }

            std::sort(out.begin(), out.end(), [](const LineBox& a, const LineBox& b) {
                const float ay = (a.top + a.bottom) * .5f;
                const float by = (b.top + b.bottom) * .5f;
                if (std::fabs(ay - by) > 2.f) return ay < by;
                return a.left < b.left;
            });
            WeShotDiag::append(std::format(L"local-geometry windows-ocr image={}x{} lines={}", width, height, out.size()));
            winrt::uninit_apartment();
            apartmentReady = false;
            return !out.empty();
        }
        catch (...) {
            if (apartmentReady) { try { winrt::uninit_apartment(); } catch (...) {} }
            WeShotDiag::append(L"local-geometry windows-ocr failed");
            return false;
        }
    }

    inline bool collect(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        bool ok = false;
        // Always run WinRT OCR in its own MTA so this helper is safe when called from
        // either the UI thread or an existing WinRT apartment.
        std::thread worker([&]() { ok = collectWorker(pixels, width, height, out); });
        worker.join();
        return ok;
    }

    inline void stabilize(std::vector<GeminiClient::TranslationBlock>& blocks,
        const std::vector<BYTE>& pixels, int width, int height, const wchar_t* path)
    {
        if (blocks.empty() || pixels.empty() || width <= 0 || height <= 0) return;
        std::vector<LineBox> lines;
        if (!collect(pixels, width, height, lines) || lines.empty()) {
            WeShotDiag::append(std::format(L"local-geometry path={} applied=0 fallback=gemini", path ? path : L"?"));
            return;
        }

        std::vector<size_t> order(blocks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const auto& x = blocks[a]; const auto& y = blocks[b];
            const int cyX = x.ymin + x.ymax, cyY = y.ymin + y.ymax;
            if (cyX != cyY) return cyX < cyY;
            return x.xmin < y.xmin;
        });

        std::vector<bool> used(lines.size(), false);
        size_t applied = 0;
        for (size_t oi = 0; oi < order.size(); ++oi) {
            auto& block = blocks[order[oi]];
            int need = std::clamp(block.sourceLines, 1, 20);

            std::vector<size_t> chosen;
            if (blocks.size() == 1) {
                // One Gemini region normally means one visual paragraph/message. Use all
                // physical OCR lines in that region so line-count jitter cannot shrink it.
                for (size_t i = 0; i < lines.size(); ++i) chosen.push_back(i);
            }
            else {
                const float bx1 = width * block.xmin / 1000.f;
                const float bx2 = width * block.xmax / 1000.f;
                const float by1 = height * block.ymin / 1000.f;
                const float by2 = height * block.ymax / 1000.f;
                const float bcx = (bx1 + bx2) * .5f;
                const float bcy = (by1 + by2) * .5f;
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
                    const float dy = std::fabs(lcy - bcy) / std::max(1, height);
                    const float score = dy * 3.f + dx * .35f + (1.f - std::clamp(overlapRatio, 0.f, 1.f)) * .8f;
                    candidates.push_back({ i, score });
                }
                std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
                if ((int)candidates.size() < need) continue;
                for (int n = 0; n < need; ++n) chosen.push_back(candidates[(size_t)n].index);
            }

            if (chosen.empty()) continue;
            float l = std::numeric_limits<float>::max();
            float t = std::numeric_limits<float>::max();
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

            // OCR word boxes describe glyph ink, while translation needs a line/paragraph
            // layout box. Expand by a fraction of the measured glyph height rather than a
            // fixed number of screenshot pixels.
            const float padX = medianH * .18f;
            const float padY = medianH * .28f;
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

        WeShotDiag::append(std::format(L"local-geometry path={} lines={} blocks={} applied={} fallback={}",
            path ? path : L"?", lines.size(), blocks.size(), applied,
            applied == blocks.size() ? L"none" : L"partial-gemini"));
    }
}
'@
Set-Content 'Src\WeShotTextGeometry.h' $helper -Encoding utf8

# Direct screenshot translation overlay.
$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('#include "WeShotTextGeometry.h"')) {
    $src = $src.Replace('#include "GeminiClient.h"', "#include \"GeminiClient.h\"`r`n#include \"WeShotTextGeometry.h\"")
}
$oldCtor = @'
        {
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
        }
'@
$newCtor = @'
        {
            WeShotTextGeometry::stabilize(this->blocks, this->pixels, imageW, imageH, L"direct");
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
        }
'@
# There are two constructors with the same body (loading + translation). Replace only
# the one after TranslationOverlay's initializer using a scoped regex.
$pattern = '(?s)(TranslationOverlay\(int screenX, int screenY, int imageW, int imageH,.*?borderWidth\(borderWidth\)\r?\n)        \{\r?\n            x = screenX; y = screenY; w = \(float\)imageW; h = \(float\)imageH;\r?\n            disableWinAnimation\(\);\r?\n        \}'
$replacement = '$1' + $newCtor.TrimEnd("`r","`n")
$patched = [regex]::Replace($src, $pattern, $replacement, 1)
if ($patched -eq $src) { throw 'v0.8.20 direct TranslationOverlay constructor target not found' }
Set-Content $path $patched -Encoding utf8

# OCR/result window: refine Gemini translation blocks with local physical OCR geometry
# immediately before they become the displayed translation blocks.
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('#include "WeShotTextGeometry.h"')) {
    $src = $src.Replace('#include "GeminiClient.h"', "#include \"GeminiClient.h\"`r`n#include \"WeShotTextGeometry.h\"")
}
$oldAssign = @'
            translatedText = std::move(result.translatedText);
            translationBlocks = std::move(result.blocks);
'@
$newAssign = @'
            WeShotTextGeometry::stabilize(result.blocks, pixels, imageW, imageH, L"result");
            translatedText = std::move(result.translatedText);
            translationBlocks = std::move(result.blocks);
'@
if (-not $src.Contains($oldAssign)) { throw 'v0.8.20 result translation assignment target not found' }
$src = $src.Replace($oldAssign, $newAssign)
Set-Content $path $src -Encoding utf8

foreach ($p in @('Src\WeShotCaptureTranslate.h','Src\WeShotOcrV2.h','Src\WeShotTextGeometry.h')) {
    $v = Get-Content $p -Raw
    if (-not $v.Contains('WeShotTextGeometry')) { throw "v0.8.20 verification failed in $p" }
}
Write-Host 'v0.8.20 local Windows OCR geometry stabilization applied.'
