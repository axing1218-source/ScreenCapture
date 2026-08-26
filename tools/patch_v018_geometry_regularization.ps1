$ErrorActionPreference = 'Stop'

$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

$old = @'
        ymin = std::clamp(normY(ry1), 0, 1000);
        xmin = std::clamp(normX(rx1), 0, 1000);
        ymax = std::clamp(normY(ry2), 0, 1000);
        xmax = std::clamp(normX(rx2), 0, 1000);

        WeShotDiag::append(std::format(
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, ymin, xmin, ymax, xmax, lines));
        return ymax > ymin && xmax > xmin;
'@

$new = @'
        ymin = std::clamp(normY(ry1), 0, 1000);
        xmin = std::clamp(normX(rx1), 0, 1000);
        ymax = std::clamp(normY(ry2), 0, 1000);
        xmax = std::clamp(normX(rx2), 0, 1000);

        // Gemini can occasionally return a region whose coordinate SCALE is valid,
        // but whose geometry is impossible for the source text (for example a whole
        // sentence in a box only 1-2 physical pixels tall).  Repair that at the parser
        // boundary using source-content geometry, not screenshot-size buckets.
        //
        // The source font can be estimated independently from width and height:
        //   fontW ~= regionWidth / sourceLineGlyphUnits
        //   fontH ~= regionHeight / (lineHeightFactor * sourceLines)
        // A large disagreement means one axis of the returned box is under-sized.
        // We only EXPAND the implausibly small axis; we never shrink a valid box.
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

        WeShotDiag::append(std::format(
            L"translate-box image={}x{} raw=[{},{},{},{}] coord={} scoreN={:.3f} scoreP={:.3f} norm0=[{},{},{},{}] geom={} fontW={:.2f} fontH={:.2f} norm=[{},{},{},{}] lines={}",
            imageW, imageH, ry1, rx1, ry2, rx2, usePixels ? L"pixels" : L"norm1000",
            scoreNorm, scorePixel, beforeYmin, beforeXmin, beforeYmax, beforeXmax,
            geometryFix, fontFromW, fontFromH, ymin, xmin, ymax, xmax, lines));
        return ymax > ymin && xmax > xmin;
'@

if (-not $src.Contains($old)) { throw 'v0.8.18 target not found: v0.8.17 translation-box log block' }
$src = $src.Replace($old, $new)
Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
foreach ($needle in @('geometryFix = L"expandY"', 'geometryFix = L"expandX"', 'fontW={:.2f}', 'norm0=[')) {
    if (-not $verify.Contains($needle)) { throw "v0.8.18 verification failed: $needle" }
}
Write-Host 'v0.8.18 translation box geometry regularization applied.'
