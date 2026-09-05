from pathlib import Path

cpp_path = Path('Src/Win/CutMask.cpp')
h_path = Path('Src/Win/CutMask.h')
cpp = cpp_path.read_text(encoding='utf-8')
h = h_path.read_text(encoding='utf-8')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, got {count}')
    return text.replace(old, new, 1)


def replace_function(text: str, start_sig: str, next_sig: str, new_body: str) -> str:
    start = text.find(start_sig)
    if start < 0:
        raise SystemExit(f'missing function: {start_sig}')
    end = text.find(next_sig, start)
    if end < 0:
        raise SystemExit(f'missing next function: {next_sig}')
    return text[:start] + new_body.rstrip() + '\n\n' + text[end:]

# Separate the three Snipaste-style surfaces. On a pure white background these
# reproduce approximately 111 (size label), 67 (magnifier), and 69 (help panel).
old_brushes = '''\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x003043), brushPanelBg.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0xAFC2CE, .92f), brushPanelBorder.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0xAFC2CE, .96f), brushKeyBorder.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0, .28f), brushAccentSoft.GetAddressOf());
'''
new_brushes = '''\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, .737f), brushPanelBg.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, .737f), brushPanelBorder.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, .729f), brushHelpBg.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, .565f), brushLabelBg.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, .64f), brushKeyBorder.GetAddressOf());
\td2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0, .34f), brushAccentSoft.GetAddressOf());
'''
cpp = replace_once(cpp, old_brushes, new_brushes, 'brush setup')

new_make_layout = r'''void CutMask::makeLayout()
{
	layout.Reset();
	layoutRect = {};
	if (!hasRect()) return;

	const int width = std::max(0, (int)std::lround(maskRect.right - maskRect.left));
	const int height = std::max(0, (int)std::lround(maskRect.bottom - maskRect.top));
	const auto text = std::format(L"{} x {} px", width, height);
	layout = Ling::D2D::get()->makeTextLayout(text, 11.f * win->dpi);
	if (!layout) return;

	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	const float scale = win->dpi;
	const float boxH = 30.f * scale;
	const float padX = 7.f * scale;
	const float gap = 3.f * scale;
	const float boxW = tm.width + padX * 2.f;
	const auto r = maskRect;

	// Hard rule: the size label may never cover even one pixel of the capture.
	// Try the four outside corners first, then the two vertical sides. If a
	// fullscreen/edge-to-edge selection leaves no external room, hide it.
	const std::array<D2D1_RECT_F, 8> candidates{
		D2D1::RectF(r.left,             r.top - gap - boxH, r.left + boxW,  r.top - gap),
		D2D1::RectF(r.right - boxW,     r.top - gap - boxH, r.right,        r.top - gap),
		D2D1::RectF(r.left,             r.bottom + gap,      r.left + boxW,  r.bottom + gap + boxH),
		D2D1::RectF(r.right - boxW,     r.bottom + gap,      r.right,        r.bottom + gap + boxH),
		D2D1::RectF(r.left - gap-boxW,  r.top,               r.left-gap,     r.top + boxH),
		D2D1::RectF(r.right + gap,      r.top,               r.right+gap+boxW, r.top + boxH),
		D2D1::RectF(r.left - gap-boxW,  r.bottom-boxH,       r.left-gap,     r.bottom),
		D2D1::RectF(r.right + gap,      r.bottom-boxH,       r.right+gap+boxW, r.bottom)
	};

	auto insideWindow = [this](const D2D1_RECT_F& c) {
		return c.left >= 0.f && c.top >= 0.f && c.right <= win->w && c.bottom <= win->h;
	};
	auto overlapsCapture = [r](const D2D1_RECT_F& c) {
		return c.left < r.right && c.right > r.left && c.top < r.bottom && c.bottom > r.top;
	};

	bool placed = false;
	for (const auto& candidate : candidates) {
		if (!insideWindow(candidate) || overlapsCapture(candidate)) continue;
		layoutRect = candidate;
		placed = true;
		break;
	}
	if (!placed) {
		layout.Reset();
		layoutRect = {};
		return;
	}

	layout->SetMaxWidth(std::max(1.f, boxW - padX * 2.f));
	layout->SetMaxHeight(boxH);
}'''
cpp = replace_function(cpp, 'void CutMask::makeLayout()', 'void CutMask::startMakeRect', new_make_layout)

new_magnifier = r'''void CutMask::paintMagnifier(ID2D1DeviceContext* ctx)
{
	if (!ctx || hideLabel) return;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || !cap->screenImg) return;
	if (cap->stage != WinCap::CapStage::Select && cap->stage != WinCap::CapStage::Adjust) return;

	POINT live{};
	GetCursorPos(&live);
	ScreenToClient(win->hwnd, &live);
	cursorPos = live;
	if (live.x < 0 || live.y < 0 || live.x >= (int)std::lround(win->w) || live.y >= (int)std::lround(win->h))
		return;

	// Once there is a capture rectangle, the magnifier belongs to that rectangle:
	// inside -> visible, outside -> completely hidden (same interaction as Snipaste).
	if (hasRect() && !pointInRect(maskRect, live)) return;

	const float scale = win->dpi;
	const float panelW = 179.f * scale;
	const float imageH = 122.f * scale;
	const float infoH = 95.f * scale;
	const float panelH = imageH + infoH;
	const float dx = 8.f * scale;
	const float dy = 20.f * scale;

	float left = live.x + dx;
	float top = live.y + dy;
	if (left + panelW > win->w) left = live.x - dx - panelW;
	if (top + panelH > win->h) top = live.y - dy - panelH;
	left = std::clamp(left, 0.f, std::max(0.f, win->w - panelW));
	top = std::clamp(top, 0.f, std::max(0.f, win->h - panelH));

	const auto panelRect = D2D1::RectF(left, top, left + panelW, top + panelH);
	ctx->FillRectangle(panelRect, brushPanelBg.Get());

	// Snipaste reference at 100% DPI: 179x217 overall, with a 1 px dark frame
	// around a 177x121 pixel-view area. The current source pixel is centered.
	const float frame = std::max(1.f, scale);
	const auto imageRect = D2D1::RectF(left + frame, top + frame,
		left + panelW - frame, top + imageH);
	const int cols = 15;
	const int rows = 10;
	const int centerCol = cols / 2;
	const int centerRow = rows / 2;
	const int wantL = live.x - centerCol;
	const int wantT = live.y - centerRow;
	const int validL = std::max(wantL, 0);
	const int validT = std::max(wantT, 0);
	const int validR = std::min(wantL + cols, (int)std::lround(win->w));
	const int validB = std::min(wantT + rows, (int)std::lround(win->h));
	const float cellW = (imageRect.right - imageRect.left) / cols;
	const float cellH = (imageRect.bottom - imageRect.top) / rows;

	if (validR > validL && validB > validT) {
		D2D1_RECT_F src{ (float)validL, (float)validT, (float)validR, (float)validB };
		D2D1_RECT_F dst{
			imageRect.left + (validL - wantL) * cellW,
			imageRect.top + (validT - wantT) * cellH,
			imageRect.left + (validR - wantL) * cellW,
			imageRect.top + (validB - wantT) * cellH
		};
		ctx->DrawBitmap(cap->screenImg.Get(), dst, 1.f,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
	}

	// The reference cross is about 7 px wide. It passes behind an 11 px square
	// representing exactly one source pixel. Redraw that pixel after the cross so
	// the centre is a clean color square with only a black outline.
	const float centerX = (imageRect.left + imageRect.right) * .5f;
	const float centerY = (imageRect.top + imageRect.bottom) * .5f;
	const float crossW = 7.f * scale;
	ctx->FillRectangle(D2D1::RectF(imageRect.left, centerY - crossW * .5f,
		imageRect.right, centerY + crossW * .5f), brushAccentSoft.Get());
	ctx->FillRectangle(D2D1::RectF(centerX - crossW * .5f, imageRect.top,
		centerX + crossW * .5f, imageRect.bottom), brushAccentSoft.Get());

	const float square = 11.f * scale;
	const auto centerCell = D2D1::RectF(centerX - square * .5f, centerY - square * .5f,
		centerX + square * .5f, centerY + square * .5f);
	const float border = std::max(1.f, scale);
	const auto centerInner = D2D1::RectF(centerCell.left + border, centerCell.top + border,
		centerCell.right - border, centerCell.bottom - border);
	D2D1_RECT_F onePixel{ (float)live.x, (float)live.y, (float)live.x + 1.f, (float)live.y + 1.f };
	ctx->DrawBitmap(cap->screenImg.Get(), centerInner, 1.f,
		D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &onePixel);
	ctx->DrawRectangle(centerCell, brushCenterBorder.Get(), border);

	const COLORREF color = sampleCapturedPixel(live);
	auto d2d = Ling::D2D::get();
	auto drawLine = [&](const std::wstring& text, float y, float fontSize, float xOffset = 0.f) {
		auto tl = d2d->makeTextLayout(text, fontSize * scale);
		if (!tl) return;
		ctx->DrawTextLayout({ left + 9.f * scale + xOffset, y }, tl.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	};

	const float infoTop = top + imageH;
	const POINT screenPos{ live.x + win->x, live.y + win->y };
	drawLine(std::format(L"({}, {})", screenPos.x, screenPos.y), infoTop + 7.f * scale, 10.f);

	const float swatchSize = 11.f * scale;
	const float swatchTop = infoTop + 31.f * scale;
	ComPtr<ID2D1SolidColorBrush> swatch;
	ctx->CreateSolidColorBrush(D2D1::ColorF(
		GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1.f), swatch.GetAddressOf());
	if (swatch) ctx->FillRectangle(D2D1::RectF(left + 10.f * scale, swatchTop,
		left + 10.f * scale + swatchSize, swatchTop + swatchSize), swatch.Get());
	ctx->DrawRectangle(D2D1::RectF(left + 10.f * scale, swatchTop,
		left + 10.f * scale + swatchSize, swatchTop + swatchSize),
		brushText.Get(), std::max(1.f, scale));
	drawLine(colorText(color), infoTop + 29.f * scale, 10.f, 29.f * scale);
	drawLine(L"按 C 复制颜色值", infoTop + 51.f * scale, 9.f);
	drawLine(L"按 Shift 切换 RGB/HEX", infoTop + 71.f * scale, 9.f);
}'''
cpp = replace_function(cpp, 'void CutMask::paintMagnifier(ID2D1DeviceContext* ctx)', 'void CutMask::paintHelp', new_magnifier)

new_help = r'''void CutMask::paintHelp(ID2D1DeviceContext* ctx)
{
	if (!ctx || hideLabel) return;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || cap->stage != WinCap::CapStage::Select) return;

	const float scale = win->dpi;
	const bool dragging = cap->isPress;
	const float panelW = (dragging ? 299.f : 328.f) * scale;
	const float panelH = (dragging ? 106.f : 199.f) * scale;
	const float margin = 14.f * scale;
	const float left = margin;
	const float top = std::max(0.f, win->h - margin - panelH);
	const auto panel = D2D1::RectF(left, top, left + panelW, top + panelH);

	// The white-background Snipaste reference resolves to ~RGB(69,69,69), i.e.
	// a black panel around 73% opacity. It has no bright outer outline.
	ctx->FillRectangle(panel, brushHelpBg.Get());

	auto d2d = Ling::D2D::get();
	auto drawRow = [&](float rowY, const std::vector<std::wstring>& keys, const std::wstring& desc) {
		float x = left + 10.f * scale;
		for (const auto& key : keys) {
			auto keyLayout = d2d->makeTextLayout(key, 10.f * scale);
			if (!keyLayout) continue;
			DWRITE_TEXT_METRICS km{};
			keyLayout->GetMetrics(&km);
			const float kw = std::max(18.f * scale, km.width + 8.f * scale);
			const float kh = 17.f * scale;
			ctx->DrawRectangle(D2D1::RectF(x, rowY, x + kw, rowY + kh),
				brushKeyBorder.Get(), std::max(1.f, scale));
			ctx->DrawTextLayout({ x + (kw - km.width) * .5f, rowY + 1.f * scale },
				keyLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
			x += kw + 4.f * scale;
		}
		auto descLayout = d2d->makeTextLayout(desc, 10.f * scale);
		if (descLayout) ctx->DrawTextLayout({ x + 6.f * scale, rowY + 1.f * scale },
			descLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	};

	const float firstY = top + 13.f * scale;
	const float step = 31.f * scale;
	drawRow(firstY + step * 0, { L"W",L"A",L"S",L"D" }, L"将鼠标指针移动 1 像素");
	drawRow(firstY + step * 1, { L"Tab" }, L"切换检测窗口 / 检测界面元素");
	drawRow(firstY + step * 2, { L"Ctrl",L"A" }, L"设置截屏区域为当前屏幕 / 全屏");
	if (!dragging) {
		drawRow(firstY + step * 3, { L"R",L"Shift",L"R" }, L"使用上一次截屏的区域");
		drawRow(firstY + step * 4, { L",",L"." }, L"回溯截屏区域历史");
		drawRow(firstY + step * 5, { L"C",L"Shift" }, L"复制颜色 / 切换 RGB/HEX");
	}
}'''
cpp = replace_function(cpp, 'void CutMask::paintHelp(ID2D1DeviceContext* ctx)', 'void CutMask::suppressLegacyMagnifier', new_help)

# Size tag has its own lighter translucent black background and no bright outline.
old_label_paint = '''\tif (!hideLabel && layout) {
\t\tctx->FillRectangle(layoutRect, brushPanelBg.Get());
\t\tctx->DrawRectangle(layoutRect, brushPanelBorder.Get(), std::max(1.f, win->dpi));
'''
new_label_paint = '''\tif (!hideLabel && layout) {
\t\tctx->FillRectangle(layoutRect, brushLabelBg.Get());
'''
cpp = replace_once(cpp, old_label_paint, new_label_paint, 'size label paint')

new_copy = r'''bool CutMask::copyCurrentColor()
{
	if (!win || !win->hwnd) return false;
	POINT p{};
	GetCursorPos(&p);
	ScreenToClient(win->hwnd, &p);
	if (p.x < 0 || p.y < 0 || p.x >= (int)std::lround(win->w) || p.y >= (int)std::lround(win->h))
		return false;
	// C only acts while the magnifier is actually available.
	if (hasRect() && !pointInRect(maskRect, p)) return false;
	cursorPos = p;
	Ling::Util::setTextToClipboard(colorText(sampleCapturedPixel(p)));
	return true;
}'''
cpp = replace_function(cpp, 'void CutMask::copyCurrentColor()', 'void CutMask::moveCursorBy', new_copy)

old_c_key = '''\tif (!ctrl && !alt && key == 'C') {
\t\tcopyCurrentColor();
\t\treturn;
\t}
'''
new_c_key = '''\tif (!ctrl && !alt && key == 'C') {
\t\tif (copyCurrentColor()) cap->close();
\t\treturn;
\t}
'''
cpp = replace_once(cpp, old_c_key, new_c_key, 'C key behavior')

# Header changes.
h = replace_once(h, '\tvoid copyCurrentColor();\n', '\tbool copyCurrentColor();\n', 'copy declaration')
h = replace_once(
    h,
    '\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPanelBorder;\n\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushKeyBorder;\n',
    '\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPanelBorder;\n\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHelpBg;\n\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLabelBg;\n\tMicrosoft::WRL::ComPtr<ID2D1SolidColorBrush> brushKeyBorder;\n',
    'brush declarations')

# Keep comments aligned with the now-hard outside-label and inside-only magnifier behavior.
h = h.replace('// - 选区左上角只显示宽高；', '// - 尺寸标签只显示在选区外，外部无空间时隐藏；')
h = h.replace('// - 鼠标右下显示像素放大镜、坐标和 RGB/HEX 取色；', '// - 鼠标在选区内时，于右下显示像素放大镜、坐标和 RGB/HEX 取色；')

cpp_path.write_text(cpp, encoding='utf-8', newline='\n')
h_path.write_text(h, encoding='utf-8', newline='\n')
print('capture UX v3 patch applied')
