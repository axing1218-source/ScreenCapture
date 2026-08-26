$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# 1) Translation blocks carry visual layout metadata.  The goal is the same
#    region/line-height oriented rendering model used by mature image
#    translators: one coherent paragraph/heading per region rather than one
#    translated fragment per OCR line.
# -----------------------------------------------------------------------------
$path = 'Src\GeminiClient.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring translation;
    };
'@ @'
    struct TranslationBlock
    {
        int ymin{ 0 }, xmin{ 0 }, ymax{ 0 }, xmax{ 0 };
        std::wstring translation;
        std::wstring role{ L"body" };
        int sourceLines{ 1 };
    };
'@ 'translation block visual metadata'

Replace-Checked ([ref]$src) @'
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"translation", strType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
'@ @'
        JsonObject intType; intType.SetNamedValue(L"type", JsonValue::CreateStringValue(L"integer"));
        JsonObject blockProps;
        blockProps.SetNamedValue(L"box_2d", makeBoxSchema());
        blockProps.SetNamedValue(L"translation", strType);
        blockProps.SetNamedValue(L"role", strType);
        blockProps.SetNamedValue(L"source_lines", intType);
        JsonArray blockReq;
        blockReq.Append(JsonValue::CreateStringValue(L"box_2d"));
        blockReq.Append(JsonValue::CreateStringValue(L"translation"));
        blockReq.Append(JsonValue::CreateStringValue(L"role"));
        blockReq.Append(JsonValue::CreateStringValue(L"source_lines"));
'@ 'translation schema visual metadata'

Replace-Checked ([ref]$src) @'
        auto req = makeBaseRequest(modelId,
            L"Read ALL readable text in this screenshot from top to bottom, including a tall/long screenshot. "
            L"Return source_text exactly as read in natural reading order, then translate all of it into " + targetLanguage +
            L" as translated_text. Also return translation blocks with box_2d=[ymin,xmin,ymax,xmax] normalized 0-1000. "
            L"Do not omit later/bottom sections of a long screenshot. Do not summarize, add commentary, generate, or edit an image. "
            L"Keep UI translations concise enough to fit their original regions.",
            &png, makeTranslationSchema(), 16384);
'@ @'
        auto req = makeBaseRequest(modelId,
            L"Read ALL readable text in this screenshot from top to bottom, including a tall/long screenshot. "
            L"Return source_text exactly as read in natural reading order, then translate all of it into " + targetLanguage +
            L" as translated_text. Preserve paragraph breaks in translated_text. "
            L"For blocks, group text by VISUAL REGION: one title, heading, paragraph, caption, or short UI label per block. "
            L"Merge adjacent wrapped lines that belong to the same paragraph; DO NOT make a separate block for every visual line. "
            L"Each block must contain box_2d=[ymin,xmin,ymax,xmax] covering the whole source region, translation, "
            L"role as exactly one of title, heading, body, caption, label, and source_lines as the number of visible source lines in that region. "
            L"Keep title/body hierarchy and original reading order. Do not omit later/bottom sections of a long screenshot. "
            L"Do not summarize, add commentary, generate, or edit an image.",
            &png, makeTranslationSchema(), 16384);
'@ 'region-oriented image translation prompt'

Replace-Checked ([ref]$src) @'
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.translation = std::move(tr); out.blocks.push_back(std::move(b));
'@ @'
                auto box = item.GetNamedArray(L"box_2d", nullptr);
                auto tr = std::wstring{ item.GetNamedString(L"translation", L"") };
                auto role = std::wstring{ item.GetNamedString(L"role", L"body") };
                int sourceLines = (int)std::lround(item.GetNamedNumber(L"source_lines", 1));
                TranslationBlock b;
                if (tr.empty() || !readBox(box, b.ymin, b.xmin, b.ymax, b.xmax)) continue;
                b.translation = std::move(tr);
                b.role = role.empty() ? L"body" : std::move(role);
                b.sourceLines = std::clamp(sourceLines, 1, 100);
                out.blocks.push_back(std::move(b));
'@ 'parse translation region metadata'

Replace-Checked ([ref]$src) @'
                TranslationBlock b;
                b.ymin = sourceBlocks[id].ymin; b.xmin = sourceBlocks[id].xmin;
                b.ymax = sourceBlocks[id].ymax; b.xmax = sourceBlocks[id].xmax;
                b.translation = std::move(tr); out.blocks.push_back(std::move(b));
'@ @'
                TranslationBlock b;
                b.ymin = sourceBlocks[id].ymin; b.xmin = sourceBlocks[id].xmin;
                b.ymax = sourceBlocks[id].ymax; b.xmax = sourceBlocks[id].xmax;
                b.translation = std::move(tr);
                b.role = L"body";
                b.sourceLines = 1;
                for (wchar_t ch : sourceBlocks[id].source) if (ch == L'\n') ++b.sourceLines;
                out.blocks.push_back(std::move(b));
'@ 'text-block fallback metadata'

Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Result window: wheel zoom, explicit long-screenshot source flag, and
#    region-aware image rendering with stable typography.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
        OcrResultWindow(std::vector<BYTE> data, int imgW, int imgH)
            : pixels(std::move(data)), imageW(imgW), imageH(imgH)
'@ @'
        OcrResultWindow(std::vector<BYTE> data, int imgW, int imgH, bool fromLongScreenshot = false)
            : pixels(std::move(data)), imageW(imgW), imageH(imgH), isLongScreenshotSource(fromLongScreenshot)
'@ 'long screenshot source flag constructor'

Replace-Checked ([ref]$src) @'
            onMouseUp.add([this](POINT, bool isRight) {
                if (isRight || !imageDragging) return;
                imageDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            });
            onSizeChanged.add([this]() { clampImageOffset(); updateZoomLabel(); refresh(); });
'@ @'
            onMouseUp.add([this](POINT, bool isRight) {
                if (isRight || !imageDragging) return;
                imageDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            });
            onMouseWheel.add([this](POINT pos, float delta) {
                if (!imageCanvas || !imageCanvas->isPosIn(pos) || delta == 0.f) return;
                zoomBy(delta > 0.f ? 1.15f : (1.f / 1.15f));
            });
            onSizeChanged.add([this]() { clampImageOffset(); updateZoomLabel(); refresh(); });
'@ 'mouse wheel image zoom'

Replace-Checked ([ref]$src) @'
            const bool preferImageTranslation = longImage || blocksLookIncomplete;
'@ @'
            const bool preferImageTranslation = isLongScreenshotSource || longImage || blocksLookIncomplete;
'@ 'always use full image translation for stitched long screenshots'

# Replace the entire translated-overlay renderer.  Body text uses a shared
# source-line-height median, while headings/titles retain their hierarchy.
# Font size scales with the image itself (important for Fit mode on a very tall
# screenshot) and is only reduced when the translation truly cannot fit.
$paintPattern = '(?s)        void paintTranslationBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        void paintImage\(\)'
$paintReplacement = @'
        void paintTranslationBlocks(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            if (!ctx || !showTranslatedImage || translationBlocks.empty()) return;
            const float dw = imageRect.right - imageRect.left, dh = imageRect.bottom - imageRect.top;

            std::vector<float> bodyLineHeights;
            bodyLineHeights.reserve(translationBlocks.size());
            for (const auto& block : translationBlocks) {
                const float boxH = dh * std::max(1, block.ymax - block.ymin) / 1000.f;
                const float lineH = boxH / (float)std::max(1, block.sourceLines);
                if (block.role.empty() || block.role == L"body") bodyLineHeights.push_back(lineH);
            }
            float bodyLineH = 0.f;
            if (!bodyLineHeights.empty()) {
                std::sort(bodyLineHeights.begin(), bodyLineHeights.end());
                bodyLineH = bodyLineHeights[bodyLineHeights.size() / 2];
            }

            for (const auto& block : translationBlocks) {
                D2D1_RECT_F rect{
                    imageRect.left + dw * block.xmin / 1000.f,
                    imageRect.top + dh * block.ymin / 1000.f,
                    imageRect.left + dw * block.xmax / 1000.f,
                    imageRect.top + dh * block.ymax / 1000.f
                };
                float boxW = rect.right - rect.left;
                float boxH = rect.bottom - rect.top;
                if (boxW < .5f || boxH < .5f) continue;

                auto bgColor = sampleBackground(block);
                const float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto textColor = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, textBrush;
                ctx->CreateSolidColorBrush(bgColor, bgBrush.GetAddressOf());
                ctx->CreateSolidColorBrush(textColor, textBrush.GetAddressOf());
                if (!bgBrush || !textBrush) continue;
                ctx->FillRectangle(rect, bgBrush.Get());

                const int sourceLines = std::max(1, block.sourceLines);
                float sourceLineH = boxH / (float)sourceLines;
                float visualLineH = sourceLineH;
                if (bodyLineH > 0.f) {
                    if (block.role.empty() || block.role == L"body") visualLineH = bodyLineH;
                    else if (block.role == L"title") visualLineH = std::max(sourceLineH, bodyLineH * 1.38f);
                    else if (block.role == L"heading") visualLineH = std::max(sourceLineH, bodyLineH * 1.18f);
                    else if (block.role == L"caption") visualLineH = std::min(sourceLineH, bodyLineH * .92f);
                }

                const float padX = std::min(2.f * dpi, boxW * .025f);
                const float padY = std::min(1.f * dpi, boxH * .025f);
                const float innerW = std::max(.5f, boxW - padX * 2.f);
                const float innerH = std::max(.5f, boxH - padY * 2.f);

                // Unlike the old renderer there is no large fixed minimum. In Fit mode a long
                // screenshot may be displayed at 10-20%; translated text must shrink with it.
                float desiredFont = std::clamp(visualLineH * .76f, .65f * dpi, 34.f * dpi);
                float minFont = std::max(.55f * dpi, desiredFont * .52f);
                float fontSize = desiredFont;
                bool fitted = false;
                const float step = std::max(.12f * dpi, desiredFont / 24.f);
                for (float fs = desiredFont; fs >= minFont; fs -= step) {
                    auto probe = Ling::D2D::makeTextLayout(block.translation, fs, innerW, 8192.f * dpi);
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

        void paintImage()
'@
$patched = [regex]::Replace($src, $paintPattern, $paintReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: region-aware translation renderer' }
$src = $patched

Replace-Checked ([ref]$src) @'
        bool imageFitMode{ true };
        float imageManualScale{ 1.f };
'@ @'
        bool imageFitMode{ true };
        bool isLongScreenshotSource{ false };
        float imageManualScale{ 1.f };
'@ 'long screenshot source member'

Replace-Checked ([ref]$src) @'
    inline void showPixels(std::vector<BYTE> pixels, int width, int height)
'@ @'
    inline void showPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
'@ 'showPixels long-source parameter'

Replace-Checked ([ref]$src) @'
        activeWindow = new OcrResultWindow(pixels, width, height);
'@ @'
        activeWindow = new OcrResultWindow(pixels, width, height, fromLongScreenshot);
'@ 'OCR window long-source construction'

Replace-Checked ([ref]$src) @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height)
'@ @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
'@ 'direct translation long-source parameter'

Replace-Checked ([ref]$src) @'
        activeWindow = new OcrResultWindow(std::move(pixels), width, height);
'@ @'
        activeWindow = new OcrResultWindow(std::move(pixels), width, height, fromLongScreenshot);
'@ 'direct translation window long-source construction'

Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 3) Wrapper and long-capture call sites pass the explicit source flag so OCR
#    -> Translate and Direct Translate follow the exact same image-layout path.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotOcr.h'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
    inline void showPixels(std::vector<BYTE> pixels, int width, int height)
    {
        // Keep the same repeated-launch protection used by normal screenshot OCR.
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::showPixels(std::move(pixels), width, height);
    }
'@ @'
    inline void showPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        // Keep the same repeated-launch protection used by normal screenshot OCR.
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::showPixels(std::move(pixels), width, height, fromLongScreenshot);
    }
'@ 'public OCR long-source parameter'

Replace-Checked ([ref]$src) @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height)
    {
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::showTranslationPixels(std::move(pixels), width, height);
    }
'@ @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::showTranslationPixels(std::move(pixels), width, height, fromLongScreenshot);
    }
'@ 'public direct translation long-source parameter'
Set-Content $path $src -Encoding utf8

$path = 'Src\Win\CapLong.h'
$src = Get-Content $path -Raw
$src = $src.Replace('WeShotOcr::showPixels(std::move(data), imgW, resultH);', 'WeShotOcr::showPixels(std::move(data), imgW, resultH, true);')
$src = $src.Replace('WeShotOcr::showTranslationPixels(std::move(data), imgW, resultH);', 'WeShotOcr::showTranslationPixels(std::move(data), imgW, resultH, true);')
if (-not $src.Contains('showPixels(std::move(data), imgW, resultH, true)')) { throw 'Long OCR source flag not applied.' }
if (-not $src.Contains('showTranslationPixels(std::move(data), imgW, resultH, true)')) { throw 'Long translate source flag not applied.' }
Set-Content $path $src -Encoding utf8

foreach ($check in @(
    @{ Path='Src\GeminiClient.h'; Needle='int sourceLines{ 1 };' },
    @{ Path='Src\GeminiClient.h'; Needle='L"source_lines"' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='onMouseWheel.add' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='isLongScreenshotSource || longImage' },
    @{ Path='Src\WeShotOcrV2.h'; Needle='bodyLineHeights' },
    @{ Path='Src\Win\CapLong.h'; Needle='resultH, true' }
)) {
    $v = Get-Content $check.Path -Raw
    if (-not $v.Contains($check.Needle)) { throw "Verification failed: $($check.Needle)" }
}

Write-Host 'v0.8.12 Youdao-style region layout + wheel zoom patch applied successfully.'
