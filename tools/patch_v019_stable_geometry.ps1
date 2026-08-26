$ErrorActionPreference = 'Stop'

$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

$old = @'
        const int beforeYmin = ymin, beforeXmin = xmin, beforeYmax = ymax, beforeXmax = xmax;
        const float regionWpx = imageW * std::max(1, xmax - xmin) / 1000.f;
        const float regionHpx = imageH * std::max(1, ymax - ymin) / 1000.f;
        const float lineGlyphUnits = sourceMaxLineUnits(source, lines);
        const float fontFromW = regionWpx / std::max(.75f, lineGlyphUnits);
        const float fontFromH = regionHpx / (1.18f * (float)lines);
        std::wstring geometryFix = L"none";

        auto expandNormalizedSpan = [](int& lo, int& hi, int desiredSpan) {
            desiredSpan = std::clamp(desiredSpan, 1, 1000);
            const float center = (lo + hi) * .5f;
            int nlo = (int)std::lround(center - desiredSpan * .5f);
            int nhi = nlo + desiredSpan;
            if (nlo < 0) { nhi -= nlo; nlo = 0; }
            if (nhi > 1000) { nlo -= (nhi - 1000); nhi = 1000; }
            nlo = std::clamp(nlo, 0, 999);
            nhi = std::clamp(nhi, nlo + 1, 1000);
            lo = nlo; hi = nhi;
        };

        const float safeFW = std::max(.01f, fontFromW);
        const float safeFH = std::max(.01f, fontFromH);
        const float ratioWH = safeFW / safeFH;
        const float ratioHW = safeFH / safeFW;

        if (ratioWH > 1.65f) {
            // Width says the source font must be much larger than the box height allows.
            // Expand the vertical span to the height implied by source text width/lines.
            const float desiredHpx = std::min((float)imageH, safeFW * 1.18f * (float)lines);
            const int desiredNormH = (int)std::lround(desiredHpx * 1000.f / std::max(1, imageH));
            if (desiredNormH > ymax - ymin) {
                expandNormalizedSpan(ymin, ymax, desiredNormH);
                geometryFix = L"expandY";
            }
        }
        else if (ratioHW > 1.65f) {
            // Height says the source font must be much larger than the box width allows.
            // Expand the horizontal span to the width implied by the source line content.
            const float desiredWpx = std::min((float)imageW, safeFH * lineGlyphUnits);
            const int desiredNormW = (int)std::lround(desiredWpx * 1000.f / std::max(1, imageW));
            if (desiredNormW > xmax - xmin) {
                expandNormalizedSpan(xmin, xmax, desiredNormW);
                geometryFix = L"expandX";
            }
        }
'@

$new = @'
        const int beforeYmin = ymin, beforeXmin = xmin, beforeYmax = ymax, beforeXmax = xmax;
        const float regionWpx = imageW * std::max(1, xmax - xmin) / 1000.f;
        const float regionHpx = imageH * std::max(1, ymax - ymin) / 1000.f;
        const float lineGlyphUnits = sourceMaxLineUnits(source, lines);
        const float fontFromW = regionWpx / std::max(.75f, lineGlyphUnits);
        const float fontFromH = regionHpx / (1.18f * (float)lines);

        auto expandNormalizedSpan = [](int& lo, int& hi, int desiredSpan) {
            desiredSpan = std::clamp(desiredSpan, 1, 1000);
            const float center = (lo + hi) * .5f;
            int nlo = (int)std::lround(center - desiredSpan * .5f);
            int nhi = nlo + desiredSpan;
            if (nlo < 0) { nhi -= nlo; nlo = 0; }
            if (nhi > 1000) { nlo -= (nhi - 1000); nhi = 1000; }
            nlo = std::clamp(nlo, 0, 999);
            nhi = std::clamp(nhi, nlo + 1, 1000);
            lo = nlo; hi = nhi;
        };

        // v0.8.19: continuous geometry reconstruction.
        // Do not wait for an arbitrary mismatch threshold before repairing a box.
        // Width and height are two independent noisy measurements of the SAME source
        // font scale.  Combine them with a smooth high-order power mean.  This follows
        // the more informative/larger estimate when one axis collapses, but changes
        // normal boxes only slightly when both estimates already agree.
        const float safeFW = std::max(.01f, fontFromW);
        const float safeFH = std::max(.01f, fontFromH);
        constexpr float p = 6.f;
        float stableFont = std::pow((std::pow(safeFW, p) + std::pow(safeFH, p)) * .5f, 1.f / p);

        // A reconstructed font can never require more space than the whole image.
        // This is a geometric feasibility bound, not a screenshot-size mode rule.
        const float maxByWidth = imageW / std::max(.75f, lineGlyphUnits);
        const float maxByHeight = imageH / (1.18f * (float)lines);
        stableFont = std::clamp(stableFont, .01f, std::max(.01f, std::min(maxByWidth, maxByHeight)));

        const float desiredWpx = std::min((float)imageW, stableFont * lineGlyphUnits);
        const float desiredHpx = std::min((float)imageH, stableFont * 1.18f * (float)lines);
        const int desiredNormW = (int)std::lround(desiredWpx * 1000.f / std::max(1, imageW));
        const int desiredNormH = (int)std::lround(desiredHpx * 1000.f / std::max(1, imageH));

        bool expandedX = false, expandedY = false;
        if (desiredNormW > xmax - xmin) {
            expandNormalizedSpan(xmin, xmax, desiredNormW);
            expandedX = true;
        }
        if (desiredNormH > ymax - ymin) {
            expandNormalizedSpan(ymin, ymax, desiredNormH);
            expandedY = true;
        }
        const std::wstring geometryFix = expandedX && expandedY ? L"rebuildXY" :
            (expandedX ? L"rebuildX" : (expandedY ? L"rebuildY" : L"stable"));
'@

if (-not $src.Contains($old)) { throw 'v0.8.19 target not found: v0.8.18 geometry block' }
$src = $src.Replace($old, $new)

$oldLog = @'
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm0=[{},{},{},{}] geom={} fontW={:.2f} fontH={:.2f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, beforeYmin, beforeXmin, beforeYmax, beforeXmax,
            geometryFix, fontFromW, fontFromH, ymin, xmin, ymax, xmax, lines));
'@
$newLog = @'
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm0=[{},{},{},{}] geom={} fontW={:.2f} fontH={:.2f} stableFont={:.2f} desiredPx={:.1f}x{:.1f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, beforeYmin, beforeXmin, beforeYmax, beforeXmax,
            geometryFix, fontFromW, fontFromH, stableFont, desiredWpx, desiredHpx,
            ymin, xmin, ymax, xmax, lines));
'@
if (-not $src.Contains($oldLog)) { throw 'v0.8.19 target not found: v0.8.18 geometry log' }
$src = $src.Replace($oldLog, $newLog)

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @('continuous geometry reconstruction', 'stableFont', 'rebuildXY', 'desiredPx={:.1f}x{:.1f}')) {
    if (-not $verify.Contains($needle)) { throw "v0.8.19 verification failed: $needle" }
}
Write-Host 'v0.8.19 stable continuous translation geometry reconstruction applied.'
