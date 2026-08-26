$ErrorActionPreference = 'Stop'
$path = 'Src\WeShotTextGeometry.h'
$src = Get-Content $path -Raw

$old = @'
    inline bool collectWindows(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        bool ok = false;
        std::thread worker([&]() { ok = collectWindowsWorker(pixels, width, height, out); });
        worker.join();
        return ok;
    }
'@
$new = @'
    inline bool collectWindows(const std::vector<BYTE>& pixels, int width, int height, std::vector<LineBox>& out)
    {
        if (pixels.empty() || width <= 0 || height <= 0) return false;

        auto scaledNearest = [](const std::vector<BYTE>& src, int sw, int sh, int scale) {
            if (scale <= 1) return src;
            const int dw = sw * scale, dh = sh * scale;
            std::vector<BYTE> dst((size_t)dw * dh * 4);
            for (int y = 0; y < dh; ++y) {
                const int sy = y / scale;
                for (int x = 0; x < dw; ++x) {
                    const int sx = x / scale;
                    const size_t s = ((size_t)sy * sw + sx) * 4;
                    const size_t d = ((size_t)y * dw + x) * 4;
                    dst[d] = src[s]; dst[d + 1] = src[s + 1];
                    dst[d + 2] = src[s + 2]; dst[d + 3] = src[s + 3];
                }
            }
            return dst;
        };

        std::vector<LineBox> best;
        double bestQuality = -1.0;
        int bestScale = 0;

        // Multi-scale OCR is a standard OCR preprocessing strategy: small glyphs gain
        // enough sampling for the recognizer, while large screenshots naturally reject
        // scales that exceed OcrEngine::MaxImageDimension inside collectWindowsWorker.
        for (int scale : { 1, 2, 3, 4 }) {
            std::vector<BYTE> input = scale == 1 ? pixels : scaledNearest(pixels, width, height, scale);
            std::vector<LineBox> candidate;
            bool ok = false;
            std::thread worker([&]() {
                ok = collectWindowsWorker(input, width * scale, height * scale, candidate);
            });
            worker.join();
            if (!ok || candidate.empty()) continue;

            double area = 0.0;
            for (auto& b : candidate) {
                b.left /= scale; b.right /= scale;
                b.top /= scale; b.bottom /= scale;
                b.score /= (float)(scale * scale);
                area += std::max(1.f, b.right - b.left) * std::max(1.f, b.bottom - b.top);
            }
            // Number of physical lines is the primary completeness signal; covered ink
            // area breaks ties between scales that found the same number of lines.
            const double quality = candidate.size() * 1000000.0 + area;
            if (quality > bestQuality) {
                bestQuality = quality;
                bestScale = scale;
                best = std::move(candidate);
            }
        }

        if (best.empty()) {
            WeShotDiag::append(std::format(L"local-geometry windows-ocr multiscale image={}x{} lines=0", width, height));
            return false;
        }
        out = std::move(best);
        WeShotDiag::append(std::format(L"local-geometry windows-ocr multiscale image={}x{} scale={} lines={}",
            width, height, bestScale, out.size()));
        return true;
    }
'@

if (-not $src.Contains($old)) { throw 'v0.8.23 multiscale collectWindows target not found' }
$src = $src.Replace($old, $new)
Set-Content $path $src -Encoding utf8
Write-Host 'v0.8.23 multiscale Windows OCR geometry applied.'
