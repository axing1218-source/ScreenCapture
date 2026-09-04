#include "pch.h"
#include <array>
#include <dwmapi.h>
#include <include/Ling.h>
#include "CutMask.h"
#include "WinCap.h"
#include "../Util.h"
#include "../Setting.h"
#include "../StarCapOcr.h"
using namespace Microsoft::WRL;

CutMask::CutMask(Ling::WinBase* win) :win{ win }
{
	auto borderWidth = std::clamp(Setting::get()->getToolNum(L"capture", L"borderWidth", 2.f), 0.f, 8.f);
	strokeWidth = borderWidth * win->dpi;
	paddingTop *= win->dpi;
	paddingMargin *= win->dpi;
	auto d2d = Ling::D2D::get();
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brushText.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.46f), brushBg.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0), brushBorder.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0), brushHandle.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brushHandleOutline.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x111111, .92f), brushMagnifierBg.GetAddressOf());

	// WinCap 自己负责 Select/非按下时的原有取色放大镜。这里补齐它缺失的两段：
	// 正在拖框，以及框好后 Adjust 阶段。回调只保存光标位置，真正绘制仍统一走 paint。
	onMouseMoveToken = win->onMouseMove.add([this](POINT pos) {
		cursorPos = pos;
		auto* cap = static_cast<WinCap*>(this->win);
		if (!cap || hideLabel) return;
		const bool needMagnifier =
			(cap->stage == WinCap::CapStage::Select && cap->isPress) ||
			(cap->stage == WinCap::CapStage::Adjust);
		if (needMagnifier) this->win->refresh();
	});
	initWinRect();
}

CutMask::~CutMask()
{
	if (win) win->onMouseMove.remove(onMouseMoveToken);
}

bool CutMask::highlight(POINT pos)
{
	for (auto& rect : winRect)
	{
		if (pos.x > rect.left && pos.y > rect.top && pos.x < rect.right && pos.y < rect.bottom) {
			if (maskRect.left != rect.left || maskRect.top != rect.top ||
				maskRect.right != rect.right || maskRect.bottom != rect.bottom) {
				maskRect = rect;
				makeLayout();
				win->refresh();
				return true;
			}
			break;
		}
	}
	return false;
}

void CutMask::initWinRect()
{
	winRect.clear();
	EnumWindows([](HWND hwnd, LPARAM lparam)
		{
			if (!hwnd) return TRUE;
			if (!IsWindowVisible(hwnd)) return TRUE;
			BOOL cloaked = FALSE;
			DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
			if (cloaked) return TRUE;
			RECT rect;
			DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(RECT));
			if (rect.right - rect.left <= 6 || rect.bottom - rect.top <= 6) return TRUE;
			auto self = (CutMask*)lparam;
			auto win = self->win;
			if (rect.left < win->x) rect.left = win->x;
			if (rect.top < win->y) rect.top = win->y;
			if (rect.right > win->x + win->w) rect.right = (LONG)(win->x + win->w);
			if (rect.bottom > win->y + win->h) rect.bottom = (LONG)(win->y + win->h);
			auto x = (float)(rect.left - win->x);
			auto y = (float)(rect.top - win->y);
			auto r = (float)(rect.right - win->x);
			auto b = (float)(rect.bottom - win->y);
			self->winRect.push_back(D2D1::RectF(x, y, r, b));
			return TRUE;
		}, (LPARAM)this);
}

void CutMask::makeLayout()
{
	layout.Reset();
	auto layoutStr = std::format(L"X:{} Y:{} R:{} B:{} W:{} H:{}",
		maskRect.left, maskRect.top, maskRect.right, maskRect.bottom,
		maskRect.right - maskRect.left, maskRect.bottom - maskRect.top);
	layout = Ling::D2D::get()->makeTextLayout(layoutStr, 10 * win->dpi);
	if (!layout) return;
	DWRITE_TEXT_METRICS tm = {};
	layout->GetMetrics(&tm);
	layoutRect = D2D1::RectF(maskRect.left, maskRect.top - paddingMargin - tm.height - paddingMargin * 2,
		maskRect.left + tm.width + paddingMargin * 2, maskRect.top - paddingMargin);
	if (layoutRect.top < 0) {
		auto h = layoutRect.bottom - layoutRect.top;
		auto w = layoutRect.right - layoutRect.left;
		layoutRect.top = maskRect.top + paddingMargin / 2;
		layoutRect.bottom = layoutRect.top + h;
		layoutRect.left = maskRect.left + paddingMargin;
		layoutRect.right = layoutRect.left + w;
	}
	layout->SetMaxWidth(layoutRect.right - layoutRect.left);
	layout->SetMaxHeight(layoutRect.bottom - layoutRect.top);
}

void CutMask::startMakeRect(POINT pos)
{
	pressPos = pos;
	cursorPos = pos;
}

void CutMask::makeRect(POINT pos)
{
	cursorPos = pos;
	auto [left, right] = std::minmax(pressPos.x, pos.x);
	auto [top, bottom] = std::minmax(pressPos.y, pos.y);
	maskRect.left = (float)left;
	maskRect.right = (float)right;
	maskRect.top = (float)top;
	maskRect.bottom = (float)bottom;
	makeLayout();
	win->refresh();
}

bool CutMask::hasRect() const
{
	return maskRect.right > maskRect.left && maskRect.bottom > maskRect.top;
}

std::array<std::pair<D2D1_POINT_2F, MaskHit>, 8> CutMask::handlePoints() const
{
	const float cx = (maskRect.left + maskRect.right) * .5f;
	const float cy = (maskRect.top + maskRect.bottom) * .5f;
	return {{
		{ D2D1::Point2F(maskRect.left,  maskRect.top),    MaskHit::TopLeft },
		{ D2D1::Point2F(cx,             maskRect.top),    MaskHit::Top },
		{ D2D1::Point2F(maskRect.right, maskRect.top),    MaskHit::TopRight },
		{ D2D1::Point2F(maskRect.right, cy),              MaskHit::Right },
		{ D2D1::Point2F(maskRect.right, maskRect.bottom), MaskHit::BottomRight },
		{ D2D1::Point2F(cx,             maskRect.bottom), MaskHit::Bottom },
		{ D2D1::Point2F(maskRect.left,  maskRect.bottom), MaskHit::BottomLeft },
		{ D2D1::Point2F(maskRect.left,  cy),              MaskHit::Left }
	}};
}

MaskHit CutMask::hitTest(POINT pos) const
{
	if (!hasRect()) return MaskHit::None;
	const float px = (float)pos.x, py = (float)pos.y;
	const float hitRadius = std::max(7.f, 7.f * win->dpi);
	const float hitRadius2 = hitRadius * hitRadius;
	for (const auto& [p, hit] : handlePoints()) {
		const float dx = px - p.x, dy = py - p.y;
		if (dx * dx + dy * dy <= hitRadius2) return hit;
	}

	const float edge = std::max(4.f, 5.f * win->dpi);
	const auto& r = maskRect;
	const bool withinX = px >= r.left - edge && px <= r.right + edge;
	const bool withinY = py >= r.top - edge && py <= r.bottom + edge;
	if (withinX && std::fabs(py - r.top) <= edge) return MaskHit::Top;
	if (withinX && std::fabs(py - r.bottom) <= edge) return MaskHit::Bottom;
	if (withinY && std::fabs(px - r.left) <= edge) return MaskHit::Left;
	if (withinY && std::fabs(px - r.right) <= edge) return MaskHit::Right;
	if (px > r.left && px < r.right && py > r.top && py < r.bottom) return MaskHit::Inside;
	return MaskHit::None;
}

void CutMask::startAdjust(POINT pos)
{
	if (StarCapOcr::containsPoint(pos)) {
		adjustHit = MaskHit::None;
		return;
	}
	cursorPos = pos;
	adjustHit = hitTest(pos);
	adjustStartRect = maskRect;
	adjustPressPos = pos;
	if (adjustHit != MaskHit::None && adjustHit != MaskHit::Inside) adjust(pos);
}

void CutMask::adjust(POINT pos)
{
	cursorPos = pos;
	if (adjustHit == MaskHit::None) return;
	auto r = adjustStartRect;
	const float px = (float)pos.x, py = (float)pos.y;
	if (adjustHit == MaskHit::Inside) {
		const float rw = r.right - r.left, rh = r.bottom - r.top;
		const float left = std::clamp(r.left + px - adjustPressPos.x, 0.f, win->w - rw);
		const float top = std::clamp(r.top + py - adjustPressPos.y, 0.f, win->h - rh);
		r = D2D1::RectF(left, top, left + rw, top + rh);
	}
	else {
		const float cx = std::clamp(px, 0.f, win->w);
		const float cy = std::clamp(py, 0.f, win->h);
		switch (adjustHit)
		{
		case MaskHit::Left: r.left = cx; break;
		case MaskHit::Right: r.right = cx; break;
		case MaskHit::Top: r.top = cy; break;
		case MaskHit::Bottom: r.bottom = cy; break;
		case MaskHit::TopLeft: r.left = cx; r.top = cy; break;
		case MaskHit::TopRight: r.right = cx; r.top = cy; break;
		case MaskHit::BottomRight: r.right = cx; r.bottom = cy; break;
		case MaskHit::BottomLeft: r.left = cx; r.bottom = cy; break;
		default: break;
		}
		const float l = std::min(r.left, r.right), rr = std::max(r.left, r.right);
		const float t = std::min(r.top, r.bottom), b = std::max(r.top, r.bottom);
		r = D2D1::RectF(l, t, rr, b);
		const bool moveLeft = adjustHit == MaskHit::Left || adjustHit == MaskHit::TopLeft || adjustHit == MaskHit::BottomLeft;
		const bool moveTop = adjustHit == MaskHit::Top || adjustHit == MaskHit::TopLeft || adjustHit == MaskHit::TopRight;
		if (r.right - r.left < minSize) {
			if (moveLeft) r.left = r.right - minSize;
			else r.right = r.left + minSize;
		}
		if (r.bottom - r.top < minSize) {
			if (moveTop) r.top = r.bottom - minSize;
			else r.bottom = r.top + minSize;
		}
	}
	if (r.left == maskRect.left && r.top == maskRect.top &&
		r.right == maskRect.right && r.bottom == maskRect.bottom) return;
	maskRect = r;
	makeLayout();
	win->refresh();
}

void CutMask::paintHandles(ID2D1DeviceContext* ctx)
{
	if (!ctx || !hasRect() || hideLabel || !brushHandle) return;
	const float radius = std::max(3.8f, 4.2f * win->dpi);
	for (const auto& [p, _] : handlePoints()) {
		D2D1_ELLIPSE e{ p, radius, radius };
		if (brushHandleOutline) ctx->FillEllipse(e, brushHandleOutline.Get());
		D2D1_ELLIPSE inner{ p, std::max(1.f, radius - 1.1f * win->dpi), std::max(1.f, radius - 1.1f * win->dpi) };
		ctx->FillEllipse(inner, brushHandle.Get());
	}
}

void CutMask::paintMagnifier(ID2D1DeviceContext* ctx)
{
	if (!ctx || hideLabel || cursorPos.x == INT_MAX || cursorPos.y == INT_MAX) return;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || !cap->screenImg) return;
	const bool showDuringDrag = cap->stage == WinCap::CapStage::Select && cap->isPress;
	const bool showDuringAdjust = cap->stage == WinCap::CapStage::Adjust;
	if (!showDuringDrag && !showDuringAdjust) return;

	constexpr int srcW = 21, srcH = 13;
	const float zoom = std::max(6.f, 8.f * win->dpi);
	const float imageW = srcW * zoom, imageH = srcH * zoom;
	const float labelH = std::max(20.f, 22.f * win->dpi);
	const float panelW = imageW;
	const float panelH = imageH + labelH;
	const float gap = std::max(14.f, 16.f * win->dpi);
	float left = cursorPos.x + gap;
	float top = cursorPos.y + gap;
	if (left + panelW > win->w) left = cursorPos.x - gap - panelW;
	if (top + panelH > win->h) top = cursorPos.y - gap - panelH;
	left = std::clamp(left, 0.f, std::max(0.f, win->w - panelW));
	top = std::clamp(top, 0.f, std::max(0.f, win->h - panelH));

	const auto panel = D2D1::RectF(left, top, left + panelW, top + panelH);
	if (brushMagnifierBg) ctx->FillRectangle(panel, brushMagnifierBg.Get());

	const int wantL = cursorPos.x - srcW / 2;
	const int wantT = cursorPos.y - srcH / 2;
	const int validL = std::max(wantL, 0);
	const int validT = std::max(wantT, 0);
	const int validR = std::min(wantL + srcW, (int)std::lround(win->w));
	const int validB = std::min(wantT + srcH, (int)std::lround(win->h));
	if (validR > validL && validB > validT) {
		const float dx = left + (validL - wantL) * zoom;
		const float dy = top + (validT - wantT) * zoom;
		const auto dest = D2D1::RectF(dx, dy,
			dx + (validR - validL) * zoom, dy + (validB - validT) * zoom);
		const auto src = D2D1::RectF((float)validL, (float)validT, (float)validR, (float)validB);
		ctx->DrawBitmap(cap->screenImg.Get(), dest, 1.f,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
	}

	// 中心格是当前鼠标所在像素。用同一 #2080F0 强调，和选区控制点形成一套视觉语言。
	const float centerX = left + (srcW / 2) * zoom;
	const float centerY = top + (srcH / 2) * zoom;
	const auto center = D2D1::RectF(centerX, centerY, centerX + zoom, centerY + zoom);
	if (brushHandle) ctx->DrawRectangle(center, brushHandle.Get(), std::max(1.f, 1.5f * win->dpi));
	if (brushHandle) ctx->DrawRectangle(D2D1::RectF(left, top, left + imageW, top + imageH),
		brushHandle.Get(), std::max(1.f, win->dpi));

	const auto text = std::format(L"X:{}  Y:{}", cursorPos.x + win->x, cursorPos.y + win->y);
	auto tl = Ling::D2D::get()->makeTextLayout(text, std::max(9.f, 10.f * win->dpi), panelW - 8.f * win->dpi, labelH);
	if (tl && brushText) {
		tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		ctx->DrawTextLayout({ left + 4.f * win->dpi, top + imageH }, tl.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
}

void CutMask::paint(ID2D1DeviceContext* ctx)
{
	if (!layout) return;
	ctx->FillRectangle(D2D1::RectF(0.f, 0.f, win->w, maskRect.top), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(0.f, maskRect.bottom, win->w, win->h), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(0.f, maskRect.top, maskRect.left, maskRect.bottom), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(maskRect.right, maskRect.top, win->w, maskRect.bottom), brushBg.Get());
	if (strokeWidth > 0.f) {
		auto halfStrokeWidth{ strokeWidth / 2.f };
		ctx->DrawRectangle(D2D1::RectF(maskRect.left - halfStrokeWidth, maskRect.top - halfStrokeWidth,
			maskRect.right + halfStrokeWidth, maskRect.bottom + halfStrokeWidth), brushBorder.Get(), strokeWidth);
	}
	paintHandles(ctx);
	if (!hideLabel) {
		ctx->FillRectangle(layoutRect, brushBg.Get());
		ctx->DrawTextLayout({ layoutRect.left + paddingMargin, layoutRect.top + paddingMargin },
			layout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	}
	paintMagnifier(ctx);
}
