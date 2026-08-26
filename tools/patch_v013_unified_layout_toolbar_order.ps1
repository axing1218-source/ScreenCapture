$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# 1) Normal screenshot direct translation: use the SAME region typography model
#    as the OCR/long-screenshot viewer introduced in v0.8.12.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotCaptureTranslate.h'
$src = Get-Content $path -Raw

$paintPattern = '(?s)        void paintBlocks\(ID2D1DeviceContext\* ctx, const D2D1_RECT_F& imageRect\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        std::vector<BYTE> pixels;'
$paintReplacement = @'
        void paintBlocks(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            if (!ctx || blocks.empty()) return;
            const float dw = imageRect.right - imageRect.left;
            const float dh = imageRect.bottom - imageRect.top;

            // Use one shared body-line baseline across the screenshot.  This is the key
            // difference from the old per-box font sizing that made adjacent Chinese text
            // randomly larger/smaller. Titles/headings keep a deliberate hierarchy.
            std::vector<float> bodyLineHeights;
            bodyLineHeights.reserve(blocks.size());
            for (const auto& block : blocks) {
                const float boxH = dh * std::max(1, block.ymax - block.ymin) / 1000.f;
                const float lineH = boxH / (float)std::max(1, block.sourceLines);
                if (block.role.empty() || block.role == L"body") bodyLineHeights.push_back(lineH);
            }
            float bodyLineH = 0.f;
            if (!bodyLineHeights.empty()) {
                std::sort(bodyLineHeights.begin(), bodyLineHeights.end());
                bodyLineH = bodyLineHeights[bodyLineHeights.size() / 2];
            }

            for (const auto& block : blocks) {
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

        std::vector<BYTE> pixels;
'@
$patched = [regex]::Replace($src, $paintPattern, $paintReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: normal screenshot translation renderer' }
$src = $patched
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Normal screenshot toolbar ordering.  This ToolCap list is the shared order
#    used whenever the standard capture-selection toolbar is shown.
#    Requested order:
#    Image Mark -> QR Code -> Video -> Long Screenshot -> OCR -> Translate
# -----------------------------------------------------------------------------
$path = 'Src\Tool\ToolCap.h'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
	// “translate” 放在 OCR 右侧：第一次点击翻译，之后同一按钮原文/译文反复切换。
	std::vector<std::wstring> btnIds = { L"mark",L"long",L"video",L"ocr",L"translate",L"qrcode",L"spliter",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"\ue97f",L"\ue73e",L"\ue660",L"\ue67b",L"译",L"\ue71e",L"",L"\ue62d",L"\ue608",L"\ue6ad" };
	std::vector<std::wstring> btnTips = { L"cap.mark",L"cap.long",L"cap.video",L"cap.ocr",L"",L"cap.qrcode",L"",L"tool.close",L"tool.save",L"tool.clipboard" };
'@ @'
	// Primary feature order is fixed and identical wherever this standard capture toolbar is used:
	// 图像标记 -> 二维码 -> 录像 -> 长截图 -> 文字识别 -> 翻译
	std::vector<std::wstring> btnIds = { L"mark",L"qrcode",L"video",L"long",L"ocr",L"translate",L"spliter",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"\ue97f",L"\ue71e",L"\ue660",L"\ue73e",L"\ue67b",L"译",L"",L"\ue62d",L"\ue608",L"\ue6ad" };
	std::vector<std::wstring> btnTips = { L"cap.mark",L"cap.qrcode",L"cap.video",L"cap.long",L"cap.ocr",L"",L"",L"tool.close",L"tool.save",L"tool.clipboard" };
'@ 'standard capture toolbar order'

Set-Content $path $src -Encoding utf8

foreach ($check in @(
    @{ Path='Src\WeShotCaptureTranslate.h'; Needle='const bool centered = block.role == L"label";' },
    @{ Path='Src\WeShotCaptureTranslate.h'; Needle='bodyLineHeights[bodyLineHeights.size() / 2]' },
    @{ Path='Src\Tool\ToolCap.h'; Needle='L"mark",L"qrcode",L"video",L"long",L"ocr",L"translate"' }
)) {
    $v = Get-Content $check.Path -Raw
    if (-not $v.Contains($check.Needle)) { throw "Verification failed: $($check.Needle)" }
}

Write-Host 'v0.8.13 unified normal translation layout + toolbar ordering applied successfully.'
