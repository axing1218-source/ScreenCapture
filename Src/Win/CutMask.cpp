#include "pch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <dwmapi.h>
#include <UIAutomation.h>
#include <include/Ling.h>
#include "CutMask.h"
#include "WinCap.h"
#include "../Util.h"
#include "../Setting.h"
#include "../StarCapOcr.h"

#pragma comment(lib, "Uiautomationcore.lib")

using namespace Microsoft::WRL;

std::vector<D2D1_RECT_F> CutMask::regionHistory;

namespace
{
	bool pointInRect(const D2D1_RECT_F& r, POINT p)
	{
		return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
	}

	float rectArea(const D2D1_RECT_F& r)
	{
		return std::max(0.f, r.right - r.left) * std::max(0.f, r.bottom - r.top);
	}

	D2D1_RECT_F intersectRectF(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
	{
		return D2D1::RectF(
			std::max(a.left, b.left), std::max(a.top, b.top),
			std::min(a.right, b.right), std::min(a.bottom, b.bottom));
	}

	bool sameRectRounded(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
	{
		return std::lround(a.left) == std::lround(b.left) &&
			std::lround(a.top) == std::lround(b.top) &&
			std::lround(a.right) == std::lround(b.right) &&
			std::lround(a.bottom) == std::lround(b.bottom);
	}
}

CutMask::CutMask(Ling::WinBase* win) : win{ win }
{
	auto borderWidth = std::clamp(Setting::get()->getToolNum(L"capture", L"borderWidth", 2.f), 0.f, 8.f);
	strokeWidth = borderWidth * win->dpi;
	paddingTop *= win->dpi;
	paddingMargin *= win->dpi;

	auto d2d = Ling::D2D::get();
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brushText.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.55f), brushBg.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0), brushBorder.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0), brushHandle.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brushHandleOutline.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x062536, .96f), brushPanelBg.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0xAFC2CE, .90f), brushPanelBorder.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0xAFC2CE, .95f), brushKeyBorder.GetAddressOf());
	d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x2080F0, .30f), brushAccentSoft.GetAddressOf());

	// UI Automation 只在用户按 Tab 切到“检测界面元素”后使用。
	// 创建失败时仍然有原生 child HWND 的降级检测，不影响普通窗口吸附。
	CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(automation.ReleaseAndGetAddressOf()));

	onMouseMoveToken = win->onMouseMove.add([this](POINT pos) {
		cursorPos = pos;
		auto* cap = static_cast<WinCap*>(this->win);
		if (!cap || hideLabel) return;
		if (cap->stage == WinCap::CapStage::Select || cap->stage == WinCap::CapStage::Adjust)
			this->win->refresh();
	});
	onKeyDownToken = win->onKeyDown.add([this](UINT key) { handleCaptureKey(key); });
	onMouseUpToken = win->onMouseUp.add([this](POINT, bool isRight) {
		if (isRight) return;
		auto* cap = static_cast<WinCap*>(this->win);
		if (!cap) return;
		if (cap->stage == WinCap::CapStage::Adjust && hasRect()) historyCursor = regionHistory.size();
	});

	historyCursor = regionHistory.size();
	initWinRect();
}

CutMask::~CutMask()
{
	rememberRegion();
	if (win) {
		win->onMouseMove.remove(onMouseMoveToken);
		win->onKeyDown.remove(onKeyDownToken);
		win->onMouseUp.remove(onMouseUpToken);
	}
}

bool CutMask::validRect(const D2D1_RECT_F& rect) const
{
	return rect.right - rect.left >= minSize && rect.bottom - rect.top >= minSize;
}

void CutMask::initWinRect()
{
	winRect.clear();
	EnumWindows([](HWND hwnd, LPARAM lparam)
		{
			if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

			BOOL cloaked = FALSE;
			DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
			if (cloaked) return TRUE;

			const auto exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
			if ((exStyle & WS_EX_LAYERED) != 0) {
				COLORREF key{}; BYTE alpha{ 255 }; DWORD flags{};
				if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) &&
					(flags & LWA_ALPHA) && alpha == 0) return TRUE;
				// 与 Snipaste 的“忽略不可点击透明窗口”思路一致：纯穿透层不抢吸附。
				if (exStyle & WS_EX_TRANSPARENT) return TRUE;
			}

			RECT rect{};
			if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
				if (!GetWindowRect(hwnd, &rect)) return TRUE;
			}
			if (rect.right - rect.left <= 3 || rect.bottom - rect.top <= 3) return TRUE;

			auto self = reinterpret_cast<CutMask*>(lparam);
			auto host = self->win;
			const LONG hostL = host->x, hostT = host->y;
			const LONG hostR = host->x + (LONG)std::lround(host->w);
			const LONG hostB = host->y + (LONG)std::lround(host->h);
			rect.left = std::max(rect.left, hostL);
			rect.top = std::max(rect.top, hostT);
			rect.right = std::min(rect.right, hostR);
			rect.bottom = std::min(rect.bottom, hostB);
			if (rect.right - rect.left <= 3 || rect.bottom - rect.top <= 3) return TRUE;

			WindowCandidate item;
			item.hwnd = hwnd;
			item.rect = D2D1::RectF((float)(rect.left - hostL), (float)(rect.top - hostT),
				(float)(rect.right - hostL), (float)(rect.bottom - hostT));
			self->winRect.push_back(item);
			return TRUE;
		}, (LPARAM)this);
}

D2D1_RECT_F CutMask::monitorRectAt(POINT localPos) const
{
	POINT screenPos{ localPos.x + win->x, localPos.y + win->y };
	HMONITOR monitor = MonitorFromPoint(screenPos, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi{ sizeof(mi) };
	if (!monitor || !GetMonitorInfoW(monitor, &mi))
		return D2D1::RectF(0.f, 0.f, win->w, win->h);
	return D2D1::RectF((float)(mi.rcMonitor.left - win->x), (float)(mi.rcMonitor.top - win->y),
		(float)(mi.rcMonitor.right - win->x), (float)(mi.rcMonitor.bottom - win->y));
}

D2D1_RECT_F CutMask::detectNativeChildRect(HWND hwnd, POINT localPos, const D2D1_RECT_F& fallback) const
{
	if (!hwnd) return fallback;
	POINT screenPos{ localPos.x + win->x, localPos.y + win->y };
	HWND current = hwnd;
	RECT best{};
	if (!GetWindowRect(current, &best)) return fallback;

	for (int depth = 0; depth < 12; ++depth) {
		POINT client = screenPos;
		if (!ScreenToClient(current, &client)) break;
		HWND child = ChildWindowFromPointEx(current, client,
			CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
		if (!child || child == current) break;
		RECT childRect{};
		if (!GetWindowRect(child, &childRect)) break;
		if (!PtInRect(&childRect, screenPos)) break;
		if (childRect.right - childRect.left < 3 || childRect.bottom - childRect.top < 3) break;
		best = childRect;
		current = child;
	}

	D2D1_RECT_F r = D2D1::RectF((float)(best.left - win->x), (float)(best.top - win->y),
		(float)(best.right - win->x), (float)(best.bottom - win->y));
	const auto clipped = intersectRectF(r, fallback);
	return validRect(clipped) ? clipped : fallback;
}

D2D1_RECT_F CutMask::detectUiElementRect(HWND hwnd, POINT localPos, const D2D1_RECT_F& fallback)
{
	// 先走 UI Automation Control View。现代 Win11/UWP/浏览器里很多“元素”根本没有 child HWND，
	// 这是单纯 ChildWindowFromPoint 做不到的部分。
	if (automation && hwnd) {
		ComPtr<IUIAutomationElement> current;
		if (SUCCEEDED(automation->ElementFromHandle(hwnd, current.GetAddressOf())) && current) {
			ComPtr<IUIAutomationTreeWalker> walker;
			automation->get_ControlViewWalker(walker.GetAddressOf());
			POINT screenPos{ localPos.x + win->x, localPos.y + win->y };
			D2D1_RECT_F best = fallback;

			for (int depth = 0; walker && current && depth < 14; ++depth) {
				ComPtr<IUIAutomationElement> child;
				if (FAILED(walker->GetFirstChildElement(current.Get(), child.GetAddressOf())) || !child) break;

				ComPtr<IUIAutomationElement> bestChild;
				D2D1_RECT_F bestChildRect{};
				float bestChildArea = FLT_MAX;
				int siblingCount = 0;
				while (child && siblingCount++ < 256) {
					BOOL offscreen = FALSE;
					RECT rr{};
					if (SUCCEEDED(child->get_CurrentIsOffscreen(&offscreen)) && !offscreen &&
						SUCCEEDED(child->get_CurrentBoundingRectangle(&rr)) &&
						rr.right - rr.left >= 3 && rr.bottom - rr.top >= 3 && PtInRect(&rr, screenPos)) {
						D2D1_RECT_F local = D2D1::RectF((float)(rr.left - win->x), (float)(rr.top - win->y),
							(float)(rr.right - win->x), (float)(rr.bottom - win->y));
						local = intersectRectF(local, fallback);
						const float area = rectArea(local);
						if (validRect(local) && area > 0.f && area < bestChildArea) {
							bestChildArea = area;
							bestChildRect = local;
							bestChild = child;
						}
					}

					ComPtr<IUIAutomationElement> next;
					if (FAILED(walker->GetNextSiblingElement(child.Get(), next.GetAddressOf()))) break;
					child = next;
				}

				if (!bestChild) break;
				best = bestChildRect;
				current = bestChild;
			}

			if (validRect(best) && rectArea(best) < rectArea(fallback) * .995f) return best;
		}
	}

	// UIA 不可用/应用未暴露元素时，至少还能识别传统 Win32 child HWND。
	return detectNativeChildRect(hwnd, localPos, fallback);
}

D2D1_RECT_F CutMask::detectRegionAt(POINT pos, HWND* matchedWindow)
{
	if (matchedWindow) *matchedWindow = nullptr;
	for (const auto& item : winRect) {
		if (!pointInRect(item.rect, pos)) continue;

		// 桌面壳窗口通常覆盖整个虚拟桌面。Snipaste 在桌面空白处的体验是锁定“当前屏幕”，
		// 而不是多屏拼成一个超大矩形，所以对 Progman / WorkerW 单独处理。
		wchar_t cls[96]{};
		GetClassNameW(item.hwnd, cls, (int)std::size(cls));
		if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
			return monitorRectAt(pos);

		if (matchedWindow) *matchedWindow = item.hwnd;
		return detectUiElements ? detectUiElementRect(item.hwnd, pos, item.rect) : item.rect;
	}
	return monitorRectAt(pos);
}

void CutMask::applyDetectedRect(const D2D1_RECT_F& rect, bool refreshWindow)
{
	if (!validRect(rect)) return;
	if (sameRectRounded(rect, maskRect)) return;
	maskRect = rect;
	makeLayout();
	if (refreshWindow) win->refresh();
}

bool CutMask::highlight(POINT pos)
{
	cursorPos = pos;
	HWND matched{};
	auto rect = detectRegionAt(pos, &matched);
	if (!validRect(rect) || sameRectRounded(rect, maskRect)) return false;
	applyDetectedRect(rect, true);
	return true;
}

void CutMask::makeLayout()
{
	layout.Reset();
	if (!hasRect()) return;
	const int width = std::max(0, (int)std::lround(maskRect.right - maskRect.left));
	const int height = std::max(0, (int)std::lround(maskRect.bottom - maskRect.top));
	const auto text = std::format(L"{} x {} px", width, height);
	layout = Ling::D2D::get()->makeTextLayout(text, 10.f * win->dpi);
	if (!layout) return;

	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	const float padX = 6.f * win->dpi;
	const float padY = 3.f * win->dpi;
	const float gap = 3.f * win->dpi;
	const float boxW = tm.width + padX * 2;
	const float boxH = tm.height + padY * 2;
	float left = maskRect.left;
	float top = maskRect.top - gap - boxH;
	if (top < 0.f) top = maskRect.top;
	if (left + boxW > win->w) left = std::max(0.f, win->w - boxW);
	layoutRect = D2D1::RectF(left, top, left + boxW, top + boxH);
	layout->SetMaxWidth(boxW - padX * 2);
	layout->SetMaxHeight(boxH - padY * 2);
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
	const float hitRadius = std::max(6.f, 6.f * win->dpi);
	const float hitRadius2 = hitRadius * hitRadius;
	for (const auto& [p, hit] : handlePoints()) {
		const float dx = px - p.x, dy = py - p.y;
		if (dx * dx + dy * dy <= hitRadius2) return hit;
	}

	const float edge = std::max(4.f, 4.f * win->dpi);
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
	if (sameRectRounded(r, maskRect)) return;
	maskRect = r;
	makeLayout();
	win->refresh();
}

void CutMask::paintHandles(ID2D1DeviceContext* ctx)
{
	if (!ctx || !hasRect() || hideLabel || !brushHandle) return;
	const float radius = std::max(3.2f, 3.6f * win->dpi);
	for (const auto& [p, _] : handlePoints()) {
		D2D1_ELLIPSE outer{ p, radius, radius };
		if (brushHandleOutline) ctx->FillEllipse(outer, brushHandleOutline.Get());
		const float innerRadius = std::max(1.f, radius - 1.f * win->dpi);
		ctx->FillEllipse(D2D1_ELLIPSE{ p, innerRadius, innerRadius }, brushHandle.Get());
	}
}

COLORREF CutMask::sampleCapturedPixel(POINT pos)
{
	if (pos.x == sampledPos.x && pos.y == sampledPos.y) return sampledColor;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || !cap->screenImg || pos.x < 0 || pos.y < 0 || pos.x >= win->w || pos.y >= win->h)
		return sampledColor;

	auto ctx = Ling::D2D::get()->deviceContext.Get();
	D2D1_BITMAP_PROPERTIES1 props{};
	props.pixelFormat = cap->screenImg->GetPixelFormat();
	props.dpiX = 96.f; props.dpiY = 96.f;
	props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	ComPtr<ID2D1Bitmap1> cpu;
	if (SUCCEEDED(ctx->CreateBitmap(D2D1::SizeU(1, 1), nullptr, 0, &props, cpu.GetAddressOf())) && cpu) {
		D2D1_POINT_2U dst{ 0, 0 };
		D2D1_RECT_U src{ (UINT32)pos.x, (UINT32)pos.y, (UINT32)pos.x + 1, (UINT32)pos.y + 1 };
		if (SUCCEEDED(cpu->CopyFromBitmap(&dst, cap->screenImg.Get(), &src))) {
			D2D1_MAPPED_RECT mapped{};
			if (SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
				const BYTE b = mapped.bits[0], g = mapped.bits[1], r = mapped.bits[2];
				cpu->Unmap();
				sampledColor = RGB(r, g, b);
				sampledPos = pos;
				return sampledColor;
			}
		}
	}

	// 极少数显卡驱动不允许 staging bitmap 直接 CopyFromBitmap，退回屏幕 DC。
	// 正常路径始终读截图开始时缓存的 screenImg，因此不会把遮罩/放大镜本身采进去。
	POINT screen{ pos.x + win->x, pos.y + win->y };
	HDC dc = GetDC(nullptr);
	if (dc) {
		sampledColor = GetPixel(dc, screen.x, screen.y);
		ReleaseDC(nullptr, dc);
	}
	sampledPos = pos;
	return sampledColor;
}

std::wstring CutMask::colorText(COLORREF color) const
{
	if (colorHex)
		return std::format(L"#{:02X}{:02X}{:02X}", GetRValue(color), GetGValue(color), GetBValue(color));
	return std::format(L"{}, {}, {}", GetRValue(color), GetGValue(color), GetBValue(color));
}

void CutMask::paintMagnifier(ID2D1DeviceContext* ctx)
{
	if (!ctx || hideLabel || cursorPos.x == INT_MAX || cursorPos.y == INT_MAX) return;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || !cap->screenImg) return;
	if (cap->stage != WinCap::CapStage::Select && cap->stage != WinCap::CapStage::Adjust) return;

	// Snipaste 风格：150x100 左右的像素窗，中央像素对应当前光标；下方是一体化信息区。
	const int cols = 15, rows = 10;
	const float cell = std::max(8.f, 10.f * win->dpi);
	const float imageW = cols * cell;
	const float imageH = rows * cell;
	const float infoH = 82.f * win->dpi;
	const float panelW = imageW;
	const float panelH = imageH + infoH;

	float left = cursorPos.x - imageW * .5f;
	float top = cursorPos.y - imageH * .5f;
	if (left < 0.f) left = std::min(win->w - panelW, cursorPos.x + 14.f * win->dpi);
	if (left + panelW > win->w) left = std::max(0.f, cursorPos.x - 14.f * win->dpi - panelW);
	if (top < 0.f) top = std::min(win->h - panelH, cursorPos.y + 14.f * win->dpi);
	if (top + panelH > win->h) top = std::max(0.f, cursorPos.y - 14.f * win->dpi - panelH);
	left = std::clamp(left, 0.f, std::max(0.f, win->w - panelW));
	top = std::clamp(top, 0.f, std::max(0.f, win->h - panelH));

	const auto imageRect = D2D1::RectF(left, top, left + imageW, top + imageH);
	const auto panelRect = D2D1::RectF(left, top, left + panelW, top + panelH);
	ctx->FillRectangle(panelRect, brushPanelBg.Get());

	const int centerCol = cols / 2;
	const int centerRow = rows / 2;
	const int wantL = cursorPos.x - centerCol;
	const int wantT = cursorPos.y - centerRow;
	const int validL = std::max(wantL, 0);
	const int validT = std::max(wantT, 0);
	const int validR = std::min(wantL + cols, (int)std::lround(win->w));
	const int validB = std::min(wantT + rows, (int)std::lround(win->h));

	if (validR > validL && validB > validT) {
		D2D1_RECT_F src{ (float)validL, (float)validT, (float)validR, (float)validB };
		D2D1_RECT_F dst{
			left + (validL - wantL) * cell,
			top + (validT - wantT) * cell,
			left + (validR - wantL) * cell,
			top + (validB - wantT) * cell
		};
		ctx->DrawBitmap(cap->screenImg.Get(), dst, 1.f,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
	}

	// 中央一行/一列用主题蓝半透明覆盖，再给中心像素一圈白边，视觉上与 Snipaste 接近。
	const float cx = left + centerCol * cell;
	const float cy = top + centerRow * cell;
	ctx->FillRectangle(D2D1::RectF(cx, top, cx + cell, top + imageH), brushAccentSoft.Get());
	ctx->FillRectangle(D2D1::RectF(left, cy, left + imageW, cy + cell), brushAccentSoft.Get());
	ctx->DrawRectangle(D2D1::RectF(cx + 1.f, cy + 1.f, cx + cell - 1.f, cy + cell - 1.f),
		brushText.Get(), std::max(1.f, win->dpi));
	ctx->DrawRectangle(imageRect, brushPanelBorder.Get(), std::max(1.f, win->dpi));

	const COLORREF color = sampleCapturedPixel(cursorPos);
	auto d2d = Ling::D2D::get();
	auto drawLine = [&](const std::wstring& text, float y, float fontSize, float xOffset = 0.f) {
		auto tl = d2d->makeTextLayout(text, fontSize * win->dpi);
		if (!tl) return;
		ctx->DrawTextLayout({ left + 8.f * win->dpi + xOffset, y }, tl.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	};

	const float infoTop = top + imageH;
	const POINT screenPos{ cursorPos.x + win->x, cursorPos.y + win->y };
	drawLine(std::format(L"({}, {})", screenPos.x, screenPos.y), infoTop + 7.f * win->dpi, 10.f);

	const float swatchSize = 11.f * win->dpi;
	const float swatchTop = infoTop + 29.f * win->dpi;
	ComPtr<ID2D1SolidColorBrush> swatch;
	ctx->CreateSolidColorBrush(D2D1::ColorF(
		GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1.f), swatch.GetAddressOf());
	if (swatch) ctx->FillRectangle(D2D1::RectF(left + 9.f * win->dpi, swatchTop,
		left + 9.f * win->dpi + swatchSize, swatchTop + swatchSize), swatch.Get());
	ctx->DrawRectangle(D2D1::RectF(left + 9.f * win->dpi, swatchTop,
		left + 9.f * win->dpi + swatchSize, swatchTop + swatchSize), brushText.Get(), std::max(1.f, win->dpi));
	drawLine(colorText(color), infoTop + 27.f * win->dpi, 10.f, 27.f * win->dpi);
	drawLine(L"按 C 复制颜色值", infoTop + 47.f * win->dpi, 9.f);
	drawLine(L"按 X 切换 RGB/HEX", infoTop + 63.f * win->dpi, 9.f);
	ctx->DrawRectangle(panelRect, brushPanelBorder.Get(), std::max(1.f, win->dpi));
}

void CutMask::paintHelp(ID2D1DeviceContext* ctx)
{
	if (!ctx || hideLabel) return;
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || (cap->stage != WinCap::CapStage::Select && cap->stage != WinCap::CapStage::Adjust)) return;

	const float scale = win->dpi;
	const float panelW = 292.f * scale;
	const float panelH = 150.f * scale;
	const float margin = 10.f * scale;
	const float left = margin;
	const float top = std::max(0.f, win->h - panelH - margin);
	const auto panel = D2D1::RectF(left, top, left + panelW, top + panelH);
	ctx->FillRectangle(panel, brushPanelBg.Get());
	ctx->DrawRectangle(panel, brushPanelBorder.Get(), std::max(1.f, scale));

	auto d2d = Ling::D2D::get();
	auto drawRow = [&](float rowY, const std::vector<std::wstring>& keys, const std::wstring& desc) {
		float x = left + 9.f * scale;
		for (const auto& key : keys) {
			auto keyLayout = d2d->makeTextLayout(key, 9.f * scale);
			if (!keyLayout) continue;
			DWRITE_TEXT_METRICS km{}; keyLayout->GetMetrics(&km);
			const float kw = std::max(18.f * scale, km.width + 8.f * scale);
			const float kh = 17.f * scale;
			ctx->DrawRectangle(D2D1::RectF(x, rowY, x + kw, rowY + kh), brushKeyBorder.Get(), std::max(1.f, scale));
			ctx->DrawTextLayout({ x + (kw - km.width) * .5f, rowY + 1.f * scale }, keyLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
			x += kw + 4.f * scale;
		}
		auto descLayout = d2d->makeTextLayout(desc, 9.f * scale);
		if (descLayout) ctx->DrawTextLayout({ x + 5.f * scale, rowY + 1.f * scale }, descLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	};

	float y = top + 10.f * scale;
	drawRow(y, { L"W",L"A",L"S",L"D" }, L"将鼠标指针移动 1 像素"); y += 25.f * scale;
	drawRow(y, { L"Tab" }, L"切换检测窗口 / 检测界面元素"); y += 25.f * scale;
	drawRow(y, { L"Ctrl",L"A" }, L"设置截屏区域为当前屏幕 / 全屏"); y += 25.f * scale;
	drawRow(y, { L"R",L"Shift",L"R" }, L"使用上一次截屏的区域"); y += 25.f * scale;
	drawRow(y, { L",",L"." }, L"回溯截屏区域历史");
}

void CutMask::suppressLegacyMagnifier(ID2D1DeviceContext* ctx)
{
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || !ctx) return;
	// WinCap 原来的取色器还会在 paintPix() 里执行。这里不改它的老代码路径，
	// 只把三支画刷换成透明并清空源矩形，然后由本类完整接管显示。
	// 这样长图/录屏等其它逻辑完全不受影响。
	if (!legacyMagnifierSuppressed) {
		ComPtr<ID2D1SolidColorBrush> transparent;
		ctx->CreateSolidColorBrush(D2D1::ColorF(0, 0.f), transparent.GetAddressOf());
		if (transparent) {
			cap->brushBg = transparent;
			cap->brushText = transparent;
			cap->crossBrush = transparent;
		}
		legacyMagnifierSuppressed = true;
	}
	cap->pixSrcRect = D2D1::RectF(0, 0, 0, 0);
}

void CutMask::paint(ID2D1DeviceContext* ctx)
{
	if (!ctx) return;
	suppressLegacyMagnifier(ctx);

	// 第一次进入截图就立即锁定光标下的窗口/当前显示器，不要求用户先晃一下鼠标。
	if (!initialDetectionDone) {
		initialDetectionDone = true;
		POINT p{};
		GetCursorPos(&p);
		ScreenToClient(win->hwnd, &p);
		cursorPos = p;
		auto* cap = static_cast<WinCap*>(win);
		if (cap && cap->stage == WinCap::CapStage::Select && !cap->isPress) {
			auto rect = detectRegionAt(p);
			if (validRect(rect)) {
				maskRect = rect;
				makeLayout();
			}
		}
	}

	if (!hasRect()) {
		paintMagnifier(ctx);
		paintHelp(ctx);
		return;
	}

	ctx->FillRectangle(D2D1::RectF(0.f, 0.f, win->w, maskRect.top), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(0.f, maskRect.bottom, win->w, win->h), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(0.f, maskRect.top, maskRect.left, maskRect.bottom), brushBg.Get());
	ctx->FillRectangle(D2D1::RectF(maskRect.right, maskRect.top, win->w, maskRect.bottom), brushBg.Get());

	if (strokeWidth > 0.f) {
		const auto half = strokeWidth / 2.f;
		ctx->DrawRectangle(D2D1::RectF(maskRect.left - half, maskRect.top - half,
			maskRect.right + half, maskRect.bottom + half), brushBorder.Get(), strokeWidth);
	}

	if (!hideLabel && layout) {
		ctx->FillRectangle(layoutRect, brushPanelBg.Get());
		ctx->DrawRectangle(layoutRect, brushPanelBorder.Get(), std::max(1.f, win->dpi));
		ctx->DrawTextLayout({ layoutRect.left + 6.f * win->dpi, layoutRect.top + 3.f * win->dpi },
			layout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
	}

	paintHandles(ctx);
	paintMagnifier(ctx);
	paintHelp(ctx);
}

void CutMask::copyCurrentColor()
{
	if (cursorPos.x == INT_MAX || cursorPos.y == INT_MAX) return;
	const auto text = colorText(sampleCapturedPixel(cursorPos));
	Ling::Util::setTextToClipboard(text);
	win->refresh();
}

void CutMask::moveCursorBy(int dx, int dy)
{
	POINT p{};
	GetCursorPos(&p);
	const int minX = win->x, minY = win->y;
	const int maxX = win->x + (int)std::lround(win->w) - 1;
	const int maxY = win->y + (int)std::lround(win->h) - 1;
	p.x = std::clamp(p.x + dx, minX, maxX);
	p.y = std::clamp(p.y + dy, minY, maxY);
	SetCursorPos(p.x, p.y);
}

void CutMask::useCurrentScreenOrFull()
{
	POINT p{};
	GetCursorPos(&p);
	ScreenToClient(win->hwnd, &p);
	if (!fullScreenToggle) {
		applyDetectedRect(monitorRectAt(p), true);
		fullScreenToggle = true;
	}
	else {
		applyDetectedRect(D2D1::RectF(0.f, 0.f, win->w, win->h), true);
		fullScreenToggle = false;
	}
}

void CutMask::restoreHistory(int direction)
{
	if (regionHistory.empty()) return;
	if (historyCursor > regionHistory.size()) historyCursor = regionHistory.size();
	if (direction < 0) {
		if (historyCursor == 0) return;
		--historyCursor;
	}
	else {
		if (historyCursor + 1 >= regionHistory.size()) return;
		++historyCursor;
	}
	applyDetectedRect(regionHistory[historyCursor], true);
}

void CutMask::rememberRegion()
{
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || cap->stage == WinCap::CapStage::Select || !hasRect()) return;
	D2D1_RECT_F r = maskRect;
	r.left = std::clamp(r.left, 0.f, win->w);
	r.top = std::clamp(r.top, 0.f, win->h);
	r.right = std::clamp(r.right, 0.f, win->w);
	r.bottom = std::clamp(r.bottom, 0.f, win->h);
	if (!validRect(r)) return;
	if (!regionHistory.empty() && sameRectRounded(regionHistory.back(), r)) return;
	if (regionHistory.size() >= 20) regionHistory.erase(regionHistory.begin());
	regionHistory.push_back(r);
}

void CutMask::handleCaptureKey(UINT key)
{
	auto* cap = static_cast<WinCap*>(win);
	if (!cap || hideLabel) return;
	if (cap->stage != WinCap::CapStage::Select && cap->stage != WinCap::CapStage::Adjust) return;

	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	if (!ctrl && !alt && key == 'C') {
		copyCurrentColor();
		return;
	}
	if (!ctrl && !alt && key == 'X') {
		colorHex = !colorHex;
		win->refresh();
		return;
	}
	if (!ctrl && !alt && cap->stage == WinCap::CapStage::Select && !cap->isPress && key == VK_TAB) {
		detectUiElements = !detectUiElements;
		POINT p{}; GetCursorPos(&p); ScreenToClient(win->hwnd, &p);
		applyDetectedRect(detectRegionAt(p), true);
		return;
	}
	if (ctrl && key == 'A') {
		useCurrentScreenOrFull();
		return;
	}
	if (!ctrl && !alt && key == 'R') {
		if (!regionHistory.empty()) {
			historyCursor = regionHistory.size() - 1;
			applyDetectedRect(regionHistory.back(), true);
		}
		return;
	}
	if (!ctrl && !alt && key == VK_OEM_COMMA) {
		restoreHistory(-1);
		return;
	}
	if (!ctrl && !alt && key == VK_OEM_PERIOD) {
		restoreHistory(1);
		return;
	}
	if (!ctrl && !alt) {
		if (key == 'W') moveCursorBy(0, -1);
		else if (key == 'S') moveCursorBy(0, 1);
		else if (key == 'A') moveCursorBy(-1, 0);
		else if (key == 'D') moveCursorBy(1, 0);
	}
}
