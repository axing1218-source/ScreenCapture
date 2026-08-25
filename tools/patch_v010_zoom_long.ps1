$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# 1) OCR / translation result window: fit / 1:1 / zoom buttons + drag-to-pan.
#    This applies to both normal screenshots and long screenshots.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw

$ctorOld = @'
            onMouseDown.add([this](POINT pos, bool isRight) {
                if (isRight && imageCanvas && imageCanvas->isPosIn(pos)) showImageContextMenu();
            });
            onSizeChanged.add([this]() { refresh(); });
'@
$ctorNew = @'
            onMouseDown.add([this](POINT pos, bool isRight) {
                if (!imageCanvas || !imageCanvas->isPosIn(pos)) return;
                if (isRight) { showImageContextMenu(); return; }
                imageDragging = true;
                dragStart = pos;
                dragStartOffsetX = imageOffsetX;
                dragStartOffsetY = imageOffsetY;
                SetCapture(hwnd);
            });
            onMouseMove.add([this](POINT pos) {
                if (!imageDragging) return;
                imageOffsetX = dragStartOffsetX + (pos.x - dragStart.x);
                imageOffsetY = dragStartOffsetY + (pos.y - dragStart.y);
                clampImageOffset();
                refresh();
            });
            onMouseUp.add([this](POINT, bool isRight) {
                if (isRight || !imageDragging) return;
                imageDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            });
            onSizeChanged.add([this]() { clampImageOffset(); updateZoomLabel(); refresh(); });
'@
Replace-Checked ([ref]$src) $ctorOld $ctorNew 'ocr image drag handlers'

$openOld = '        void open() { createNativeWindow(0, WS_OVERLAPPEDWINDOW); }'
$openNew = @'
        void open() { createNativeWindow(0, WS_OVERLAPPEDWINDOW); }

        // Used by direct translation of a stitched long screenshot: open the same viewer but
        // skip a separate OCR request and immediately translate the image.
        void beginImageTranslation()
        {
            originalText.clear();
            geminiOcrBlocks.clear();
            if (textBox) textBox->setText(L"");
            startGeminiTranslation();
        }
'@
Replace-Checked ([ref]$src) $openOld $openNew 'begin direct image translation'

$leftOld = @'
            body->setBg(0xF4F4F4FF);
            body->setFlexDirection(Ling::FlexDirection::Row);
            imageCanvas = body->makeChild<Ling::Canvas>();
            imageCanvas->setFlexGrow(1.f);
            imageCanvas->setHeightPercent(100.f);
            imageCanvas->setBg(0xF2F2F2FF);
            auto divider = body->makeChild<Ling::Node>();
'@
$leftNew = @'
            body->setBg(0xF4F4F4FF);
            body->setFlexDirection(Ling::FlexDirection::Row);

            auto left = body->makeChild<Ling::Node>();
            left->setFlexGrow(1.f);
            left->setHeightPercent(100.f);
            left->setBg(0xF2F2F2FF);
            left->setFlexDirection(Ling::FlexDirection::Column);

            auto zoomRow = left->makeChild<Ling::Node>();
            zoomRow->setHeight(38.f); zoomRow->setWidthPercent(100.f);
            zoomRow->setPaddingLeft(10.f); zoomRow->setPaddingRight(10.f);
            zoomRow->setBg(0xFAFAFAFF);
            zoomRow->setFlexDirection(Ling::FlexDirection::Row);
            zoomRow->setAlignItems(Ling::Align::Center);

            zoomFitBtn = zoomRow->makeChild<Ling::Button>();
            zoomFitBtn->setText(L"适合"); zoomFitBtn->setSize(52.f, 26.f); zoomFitBtn->setFontSize(12.f);
            zoomFitBtn->setBorderRadius(4.f); zoomFitBtn->setBg(0xE8F3FFFF); zoomFitBtn->setColor(0x1677FFFF);
            zoomFitBtn->setMarginRight(6.f);
            zoomFitBtn->onClick.add([this](Ling::Button*) { setFitZoom(); });

            zoomActualBtn = zoomRow->makeChild<Ling::Button>();
            zoomActualBtn->setText(L"1:1"); zoomActualBtn->setSize(46.f, 26.f); zoomActualBtn->setFontSize(12.f);
            zoomActualBtn->setBorderRadius(4.f); zoomActualBtn->setBg(0xF3F3F3FF); zoomActualBtn->setColor(0x444444FF);
            zoomActualBtn->setMarginRight(10.f);
            zoomActualBtn->onClick.add([this](Ling::Button*) { setActualZoom(); });

            zoomOutBtn = zoomRow->makeChild<Ling::Button>();
            zoomOutBtn->setText(L"−"); zoomOutBtn->setSize(30.f, 26.f); zoomOutBtn->setFontSize(15.f);
            zoomOutBtn->setBorderRadius(4.f); zoomOutBtn->setBg(0xF3F3F3FF); zoomOutBtn->setMarginRight(4.f);
            zoomOutBtn->onClick.add([this](Ling::Button*) { zoomBy(1.f / 1.25f); });

            zoomLabel = zoomRow->makeChild<Ling::Label>();
            zoomLabel->setText(L"适合"); zoomLabel->setWidth(72.f); zoomLabel->setFontSize(12.f);
            zoomLabel->setColor(0x666666FF); zoomLabel->setTextAlign(DWRITE_TEXT_ALIGNMENT_CENTER);

            zoomInBtn = zoomRow->makeChild<Ling::Button>();
            zoomInBtn->setText(L"+"); zoomInBtn->setSize(30.f, 26.f); zoomInBtn->setFontSize(15.f);
            zoomInBtn->setBorderRadius(4.f); zoomInBtn->setBg(0xF3F3F3FF); zoomInBtn->setMarginLeft(4.f);
            zoomInBtn->onClick.add([this](Ling::Button*) { zoomBy(1.25f); });

            auto zoomHint = zoomRow->makeChild<Ling::Label>();
            zoomHint->setText(L"拖动图片可查看超出区域"); zoomHint->setFontSize(11.f); zoomHint->setColor(0x999999FF);
            zoomHint->setFlexGrow(1.f); zoomHint->setTextAlign(DWRITE_TEXT_ALIGNMENT_TRAILING);

            imageCanvas = left->makeChild<Ling::Canvas>();
            imageCanvas->setFlexGrow(1.f);
            imageCanvas->setWidthPercent(100.f);
            imageCanvas->setBg(0xF2F2F2FF);
            auto divider = body->makeChild<Ling::Node>();
'@
Replace-Checked ([ref]$src) $leftOld $leftNew 'ocr zoom toolbar layout'

$helperAnchor = '        D2D1_COLOR_F sampleBackground(const GeminiClient::TranslationBlock& block) const'
$helpers = @'
        float getFitScale() const
        {
            if (!imageCanvas || imageW <= 0 || imageH <= 0) return 1.f;
            const float margin = 18.f * dpi;
            const float availW = std::max(1.f, imageCanvas->w - margin * 2.f);
            const float availH = std::max(1.f, imageCanvas->h - margin * 2.f);
            return std::clamp(std::min(availW / imageW, availH / imageH), .02f, 8.f);
        }

        float getImageScale() const
        {
            return imageFitMode ? getFitScale() : std::clamp(imageManualScale, .02f, 8.f);
        }

        void updateZoomButtons()
        {
            if (zoomFitBtn) {
                zoomFitBtn->setBg(imageFitMode ? 0xE8F3FFFF : 0xF3F3F3FF);
                zoomFitBtn->setColor(imageFitMode ? 0x1677FFFF : 0x444444FF);
            }
            if (zoomActualBtn) {
                const bool actual = !imageFitMode && fabsf(imageManualScale - 1.f) < .001f;
                zoomActualBtn->setBg(actual ? 0xE8F3FFFF : 0xF3F3F3FF);
                zoomActualBtn->setColor(actual ? 0x1677FFFF : 0x444444FF);
            }
        }

        void updateZoomLabel()
        {
            updateZoomButtons();
            if (!zoomLabel) return;
            const int pct = (int)std::round(getImageScale() * 100.f);
            zoomLabel->setText(imageFitMode ? std::format(L"适合 {}%", pct) : std::format(L"{}%", pct));
        }

        void clampImageOffset()
        {
            if (!imageCanvas || imageW <= 0 || imageH <= 0) return;
            if (imageFitMode) { imageOffsetX = imageOffsetY = 0.f; return; }
            const float margin = 18.f * dpi;
            const float availW = std::max(1.f, imageCanvas->w - margin * 2.f);
            const float availH = std::max(1.f, imageCanvas->h - margin * 2.f);
            const float scale = getImageScale();
            const float dw = imageW * scale, dh = imageH * scale;
            const float limitX = std::max(0.f, (dw - availW) * .5f);
            const float limitY = std::max(0.f, (dh - availH) * .5f);
            imageOffsetX = std::clamp(imageOffsetX, -limitX, limitX);
            imageOffsetY = std::clamp(imageOffsetY, -limitY, limitY);
        }

        void setFitZoom()
        {
            imageFitMode = true;
            imageOffsetX = imageOffsetY = 0.f;
            updateZoomLabel();
            refresh();
        }

        void setActualZoom()
        {
            imageFitMode = false;
            imageManualScale = 1.f;
            imageOffsetX = imageOffsetY = 0.f;
            clampImageOffset();
            updateZoomLabel();
            refresh();
        }

        void zoomBy(float factor)
        {
            const float current = getImageScale();
            imageFitMode = false;
            imageManualScale = std::clamp(current * factor, .02f, 8.f);
            clampImageOffset();
            updateZoomLabel();
            refresh();
        }

'@
if (-not $src.Contains($helperAnchor)) { throw 'Patch target not found: zoom helper anchor' }
$src = $src.Replace($helperAnchor, $helpers + $helperAnchor)

$paintPattern = '(?s)        void paintImage\(\)\r?\n        \{.*?\r?\n        \}\r?\n\r?\n        bool makeTranslatedPixels'
$paintReplacement = @'
        void paintImage()
        {
            if (!imageCanvas) return;
            auto ctx = imageCanvas->startPaint(); if (!ctx) return;
            ctx->Clear(D2D1::ColorF(0xF2F2F2));
            if (imageBitmap && imageW > 0 && imageH > 0) {
                float cw = imageCanvas->w, ch = imageCanvas->h;
                float scale = getImageScale();
                float dw = imageW * scale, dh = imageH * scale;
                clampImageOffset();
                float left = (cw - dw) * .5f + imageOffsetX;
                float top = (ch - dh) * .5f + imageOffsetY;
                auto dest = D2D1::RectF(left, top, left + dw, top + dh);
                ctx->DrawBitmap(imageBitmap.Get(), dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                if (translating) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBrush, loadingTextBrush;
                    ctx->CreateSolidColorBrush(D2D1::ColorF(.38f, .38f, .38f, .46f), overlayBrush.GetAddressOf());
                    ctx->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), loadingTextBrush.GetAddressOf());
                    if (overlayBrush) ctx->FillRectangle(dest, overlayBrush.Get());
                    if (loadingTextBrush) {
                        const float loadingFont = std::clamp(16.f * dpi, 12.f * dpi, 22.f * dpi);
                        auto loadingLayout = Ling::D2D::makeTextLayout(L"正在翻译中...", loadingFont,
                            std::max(1.f, dw), std::max(1.f, dh));
                        if (loadingLayout) {
                            loadingLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            loadingLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                            ctx->DrawTextLayout({ dest.left, dest.top }, loadingLayout.Get(), loadingTextBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        }
                    }
                }
                else {
                    paintTranslationBlocks(ctx, dest);
                }
                updateZoomLabel();
            }
            imageCanvas->finishPaint();
        }

        bool makeTranslatedPixels
'@
$patched = [regex]::Replace($src, $paintPattern, $paintReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: zoom-aware paintImage' }
$src = $patched

$membersOld = @'
        Ling::Canvas* imageCanvas{ nullptr };
        Ling::TextBox* textBox{ nullptr };
        Ling::Label* status{ nullptr };
        Ling::Button* originalTab{ nullptr };
        Ling::Button* translatedTab{ nullptr };
        Ling::Button* translateBtn{ nullptr };
'@
$membersNew = @'
        Ling::Canvas* imageCanvas{ nullptr };
        Ling::TextBox* textBox{ nullptr };
        Ling::Label* status{ nullptr };
        Ling::Label* zoomLabel{ nullptr };
        Ling::Button* zoomFitBtn{ nullptr };
        Ling::Button* zoomActualBtn{ nullptr };
        Ling::Button* zoomOutBtn{ nullptr };
        Ling::Button* zoomInBtn{ nullptr };
        Ling::Button* originalTab{ nullptr };
        Ling::Button* translatedTab{ nullptr };
        Ling::Button* translateBtn{ nullptr };
        bool imageFitMode{ true };
        float imageManualScale{ 1.f };
        float imageOffsetX{ 0.f }, imageOffsetY{ 0.f };
        bool imageDragging{ false };
        POINT dragStart{ 0,0 };
        float dragStartOffsetX{ 0.f }, dragStartOffsetY{ 0.f };
'@
Replace-Checked ([ref]$src) $membersOld $membersNew 'ocr zoom state members'

$showAnchor = @'
    inline void show(WinCap* win)
    {
'@
$showTranslationFunction = @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height)
    {
        if (pixels.empty() || width <= 0 || height <= 0) return;
        if (pixels.size() < (size_t)width * (size_t)height * 4) return;
        ++requestId;
        if (activeWindow) activeWindow->close();
        activeWindow = new OcrResultWindow(std::move(pixels), width, height);
        activeWindow->open();
        activeWindow->beginImageTranslation();
    }

'@
if (-not $src.Contains($showAnchor)) { throw 'Patch target not found: showTranslationPixels anchor' }
$src = $src.Replace($showAnchor, $showTranslationFunction + $showAnchor)

Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Public OCR wrapper: expose direct image translation entry point.
# -----------------------------------------------------------------------------
$path = 'Src\WeShotOcr.h'
$src = Get-Content $path -Raw
$ocrWrapOld = @'
    inline void show(WinCap* win)
    {
'@
$ocrWrapNew = @'
    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height)
    {
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::showTranslationPixels(std::move(pixels), width, height);
    }

    inline void show(WinCap* win)
    {
'@
Replace-Checked ([ref]$src) $ocrWrapOld $ocrWrapNew 'ocr translation wrapper'
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 3) Long screenshot: add a Translate button. The stitched image opens in the
#    same result viewer, immediately enters the gray translucent loading state,
#    and gains the same fit / 1:1 / zoom controls.
# -----------------------------------------------------------------------------
$path = 'Src\Win\CapLong.h'
$src = Get-Content $path -Raw
$capLongOld = @'
	bool ocr()
	{
		if (imgData.empty() || imgW <= 0 || resultH <= 0) return false;
		stopCap();
		auto data = imgData;
		WeShotOcr::showPixels(std::move(data), imgW, resultH);
		return true;
	}
private:
'@
$capLongNew = @'
	bool ocr()
	{
		if (imgData.empty() || imgW <= 0 || resultH <= 0) return false;
		stopCap();
		auto data = imgData;
		WeShotOcr::showPixels(std::move(data), imgW, resultH);
		return true;
	}
	bool translate()
	{
		if (imgData.empty() || imgW <= 0 || resultH <= 0) return false;
		stopCap();
		auto data = imgData;
		WeShotOcr::showTranslationPixels(std::move(data), imgW, resultH);
		return true;
	}
private:
'@
Replace-Checked ([ref]$src) $capLongOld $capLongNew 'long screenshot translate method'
Set-Content $path $src -Encoding utf8

$path = 'Src\Tool\ToolLong.h'
$src = Get-Content $path -Raw
$toolVecOld = @'
	std::vector<std::wstring> btnIds = { L"auto",L"ocr",L"pin",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"▶",L"\ue67b",L"\ue6a2",L"\ue62d",L"\ue608",L"\ue6ad" };
'@
$toolVecNew = @'
	std::vector<std::wstring> btnIds = { L"auto",L"ocr",L"translate",L"pin",L"close",L"save",L"clipboard" };
	std::vector<std::wstring> btnCodes = { L"▶",L"\ue67b",L"译",L"\ue6a2",L"\ue62d",L"\ue608",L"\ue6ad" };
'@
Replace-Checked ([ref]$src) $toolVecOld $toolVecNew 'long toolbar translate button'
Set-Content $path $src -Encoding utf8

$path = 'Src\Tool\ToolLong.cpp'
$src = Get-Content $path -Raw
$toolStyleOld = @'
		if (btnIds[i] == L"auto") {
			btn->setFontFamily(L"Microsoft YaHei");
			btn->setFontSize(12.f);
			tip->bind(btn, L"自动滚动");
		}
		else {
'@
$toolStyleNew = @'
		if (btnIds[i] == L"auto" || btnIds[i] == L"translate") {
			btn->setFontFamily(L"Microsoft YaHei");
			btn->setFontSize(12.f);
			if (btnIds[i] == L"auto") tip->bind(btn, L"自动滚动");
			else tip->bind(btn, L"翻译长截图");
		}
		else {
'@
Replace-Checked ([ref]$src) $toolStyleOld $toolStyleNew 'long toolbar translate style'

$toolClickOld = @'
	if (btn->id == L"ocr") {
		if (!capLong || !capLong->ocr()) return;
		win->close();
		return;
	}
	if (btn->id == L"pin") {
'@
$toolClickNew = @'
	if (btn->id == L"ocr") {
		if (!capLong || !capLong->ocr()) return;
		win->close();
		return;
	}
	if (btn->id == L"translate") {
		if (!capLong || !capLong->translate()) return;
		win->close();
		return;
	}
	if (btn->id == L"pin") {
'@
Replace-Checked ([ref]$src) $toolClickOld $toolClickNew 'long toolbar translate click'
Set-Content $path $src -Encoding utf8

# Basic build-time verification.
$verify = Get-Content 'Src\WeShotOcrV2.h' -Raw
foreach ($needle in @(
    'zoomFitBtn->setText(L"适合")',
    'zoomActualBtn->setText(L"1:1")',
    'void zoomBy(float factor)',
    'void showTranslationPixels(std::vector<BYTE> pixels, int width, int height)',
    'void beginImageTranslation()',
    'imageFitMode{ true }'
)) {
    if (-not $verify.Contains($needle)) { throw "Verification failed: $needle" }
}
$verifyLong = Get-Content 'Src\Tool\ToolLong.h' -Raw
if (-not $verifyLong.Contains('L"translate"')) { throw 'Verification failed: long translate button' }
Write-Host 'v0.8.10 zoom + long screenshot translation patch applied successfully.'
