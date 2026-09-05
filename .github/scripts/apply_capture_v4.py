from pathlib import Path
import re

CPP = Path('Src/Win/CutMask.cpp')
WINCAP = Path('Src/Win/WinCap.cpp')

cpp = CPP.read_text(encoding='utf-8-sig')

popup_class = r'''
class MagnifierPopup : public Ling::WinBase
{
public:
    explicit MagnifierPopup(CutMask* owner) : owner(owner)
    {
        dpi = owner->win->dpi;
        setSize(179.f, 227.f);
    }

    void setCursorPoint(POINT p)
    {
        cursor = p;
        refresh();
    }

private:
    void onCreated() override
    {
        canvas = body->makeChild<Ling::Canvas>();
        canvas->enableSwapChain();
        canvas->setSizePercent(100.f, 100.f);
        show();
    }

    void layout() override
    {
        Ling::WinBase::layout();
        if (!canvas || !owner) return;
        auto ctx = canvas->startPaint();
        if (!ctx) return;
        ctx->Clear(0);
        owner->paintMagnifierPanel(ctx, cursor, 0.f, 0.f);
        canvas->finishPaint();
    }

    LRESULT onHitTest(const POINT) override
    {
        return HTTRANSPARENT;
    }

private:
    CutMask* owner{ nullptr };
    Ling::Canvas* canvas{ nullptr };
    POINT cursor{};
};
'''

marker = '\n}\n\nCutMask::CutMask(Ling::WinBase* win)'
if 'class MagnifierPopup : public Ling::WinBase' not in cpp:
    if marker not in cpp:
        raise SystemExit('namespace/CutMask constructor marker not found')
    cpp = cpp.replace(marker, '\n}\n\n' + popup_class + '\nCutMask::CutMask(Ling::WinBase* win)', 1)

old_move = '''\tonMouseMoveToken = win->onMouseMove.add([this](POINT pos) {
\t\tcursorPos = pos;
\t\tauto* cap = static_cast<WinCap*>(this->win);
\t\tif (!cap || hideLabel) return;
\t\tif (cap->stage == WinCap::CapStage::Select || cap->stage == WinCap::CapStage::Adjust)
\t\t\tthis->win->refresh();
\t});'''
new_move = '''\tonMouseMoveToken = win->onMouseMove.add([this](POINT pos) {
\t\tcursorPos = pos;
\t\tauto* cap = static_cast<WinCap*>(this->win);
\t\tif (!cap || hideLabel) {
\t\t\thideMagnifierPopup();
\t\t\treturn;
\t\t}
\t\tif (cap->stage == WinCap::CapStage::Adjust)
\t\t\tupdateMagnifierPopup(pos);
\t\telse
\t\t\thideMagnifierPopup();
\t\tif (cap->stage == WinCap::CapStage::Select || cap->stage == WinCap::CapStage::Adjust)
\t\t\tthis->win->refresh();
\t});'''
if old_move in cpp:
    cpp = cpp.replace(old_move, new_move, 1)
elif 'updateMagnifierPopup(pos);' not in cpp:
    raise SystemExit('mouse move block not found')

old_dtor = '''CutMask::~CutMask()
{
\trememberRegion();'''
new_dtor = '''CutMask::~CutMask()
{
\thideMagnifierPopup();
\tif (magnifierPopup) magnifierPopup->close();
\trememberRegion();'''
if old_dtor in cpp:
    cpp = cpp.replace(old_dtor, new_dtor, 1)

magnifier_block = r'''void CutMask::paintMagnifierPanel(ID2D1DeviceContext* ctx, POINT live, float left, float top)
{
    if (!ctx) return;
    auto* cap = static_cast<WinCap*>(win);
    if (!cap || !cap->screenImg) return;

    const float scale = win->dpi;
    const int cols = 15;
    const int rows = 10;
    const float panelW = 179.f * scale;
    const float imageH = 122.f * scale;
    const float infoH = 105.f * scale;
    const float panelH = imageH + infoH;
    const float cellW = panelW / cols;
    const float cellH = imageH / rows;
    const auto panelRect = D2D1::RectF(left, top, left + panelW, top + panelH);
    const auto imageRect = D2D1::RectF(left, top, left + panelW, top + imageH);

    ctx->FillRectangle(panelRect, brushPanelBg.Get());

    const int centerCol = cols / 2;
    const int centerRow = rows / 2;
    const int wantL = live.x - centerCol;
    const int wantT = live.y - centerRow;
    const int validL = std::max(wantL, 0);
    const int validT = std::max(wantT, 0);
    const int validR = std::min(wantL + cols, (int)std::lround(win->w));
    const int validB = std::min(wantT + rows, (int)std::lround(win->h));
    if (validR > validL && validB > validT) {
        D2D1_RECT_F src{ (float)validL, (float)validT, (float)validR, (float)validB };
        D2D1_RECT_F dst{
            left + (validL - wantL) * cellW,
            top + (validT - wantT) * cellH,
            left + (validR - wantL) * cellW,
            top + (validB - wantT) * cellH
        };
        ctx->DrawBitmap(cap->screenImg.Get(), dst, 1.f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
    }

    // Keep the pixel locator hard-edged. The blue cross and the black one-pixel
    // outline are both 10% thinner than V3 and are painted with aliased geometry.
    const auto oldAA = ctx->GetAntialiasMode();
    ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const float cx = left + centerCol * cellW;
    const float cy = top + centerRow * cellH;
    const float crossThickness = 6.3f * scale;
    const float crossHalf = crossThickness * .5f;
    const float cellCenterX = cx + cellW * .5f;
    const float cellCenterY = cy + cellH * .5f;
    ctx->FillRectangle(D2D1::RectF(left, cellCenterY - crossHalf, left + panelW, cellCenterY + crossHalf), brushAccentSoft.Get());
    ctx->FillRectangle(D2D1::RectF(cellCenterX - crossHalf, top, cellCenterX + crossHalf, top + imageH), brushAccentSoft.Get());

    const auto centerCell = D2D1::RectF(cx, cy, cx + cellW, cy + cellH);
    ctx->DrawRectangle(centerCell, brushCenterBorder.Get(), std::max(.9f, 1.35f * scale));
    ctx->SetAntialiasMode(oldAA);

    ctx->DrawRectangle(imageRect, brushPanelBorder.Get(), std::max(1.f, scale));

    const COLORREF color = sampleCapturedPixel(live);
    auto d2d = Ling::D2D::get();
    auto drawLine = [&](const std::wstring& text, float y, float fontSize, float xOffset = 0.f) {
        auto tl = d2d->makeTextLayout(text, fontSize * scale);
        if (!tl) return;
        ctx->DrawTextLayout({ left + 10.f * scale + xOffset, y }, tl.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    };

    const float infoTop = top + imageH;
    const POINT screenPos{ live.x + win->x, live.y + win->y };
    drawLine(std::format(L"({}, {})", screenPos.x, screenPos.y), infoTop + 8.f * scale, 11.5f);

    const float swatchSize = 12.f * scale;
    const float swatchTop = infoTop + 35.f * scale;
    ComPtr<ID2D1SolidColorBrush> swatch;
    ctx->CreateSolidColorBrush(D2D1::ColorF(
        GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1.f), swatch.GetAddressOf());
    if (swatch) ctx->FillRectangle(D2D1::RectF(left + 11.f * scale, swatchTop,
        left + 11.f * scale + swatchSize, swatchTop + swatchSize), swatch.Get());
    ctx->DrawRectangle(D2D1::RectF(left + 11.f * scale, swatchTop,
        left + 11.f * scale + swatchSize, swatchTop + swatchSize),
        brushText.Get(), std::max(1.f, scale));
    drawLine(colorText(color), infoTop + 32.f * scale, 11.5f, 32.f * scale);
    drawLine(L"C  复制颜色值", infoTop + 60.f * scale, 10.5f);
    drawLine(L"Shift  切换 RGB/HEX", infoTop + 82.f * scale, 10.5f);
    ctx->DrawRectangle(panelRect, brushPanelBorder.Get(), std::max(1.f, scale));
}

void CutMask::hideMagnifierPopup()
{
    if (magnifierPopup && magnifierPopup->hwnd)
        ShowWindow(magnifierPopup->hwnd, SW_HIDE);
}

void CutMask::updateMagnifierPopup(POINT live)
{
    auto* cap = static_cast<WinCap*>(win);
    if (!cap || hideLabel || cap->stage != WinCap::CapStage::Adjust ||
        !hasRect() || !pointInRect(maskRect, live)) {
        hideMagnifierPopup();
        return;
    }

    const float scale = win->dpi;
    const float panelW = 179.f * scale;
    const float panelH = 227.f * scale;
    const float dx = 8.f * scale;
    const float dy = 20.f * scale;
    float left = live.x + dx;
    float top = live.y + dy;
    if (left + panelW > win->w) left = live.x - dx - panelW;
    if (top + panelH > win->h) top = live.y - dy - panelH;
    left = std::clamp(left, 0.f, std::max(0.f, win->w - panelW));
    top = std::clamp(top, 0.f, std::max(0.f, win->h - panelH));

    const int screenX = win->x + (int)std::lround(left);
    const int screenY = win->y + (int)std::lround(top);
    if (!magnifierPopup) {
        magnifierPopup = std::make_unique<MagnifierPopup>(this);
        magnifierPopup->setPosition(screenX, screenY);
        magnifierPopup->createNativeWindow(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            WS_POPUP);
    }

    magnifierPopup->setCursorPoint(live);
    if (magnifierPopup->hwnd) {
        SetWindowPos(magnifierPopup->hwnd, HWND_TOPMOST, screenX, screenY, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void CutMask::syncMagnifier(POINT pos)
{
    auto* cap = static_cast<WinCap*>(win);
    if (cap && cap->stage == WinCap::CapStage::Adjust)
        updateMagnifierPopup(pos);
    else
        hideMagnifierPopup();
}

void CutMask::paintMagnifier(ID2D1DeviceContext* ctx)
{
    if (!ctx || hideLabel) {
        hideMagnifierPopup();
        return;
    }
    auto* cap = static_cast<WinCap*>(win);
    if (!cap || !cap->screenImg) {
        hideMagnifierPopup();
        return;
    }
    if (cap->stage != WinCap::CapStage::Select && cap->stage != WinCap::CapStage::Adjust) {
        hideMagnifierPopup();
        return;
    }

    POINT live{};
    GetCursorPos(&live);
    ScreenToClient(win->hwnd, &live);
    cursorPos = live;
    if (live.x < 0 || live.y < 0 || live.x >= (int)std::lround(win->w) || live.y >= (int)std::lround(win->h)) {
        hideMagnifierPopup();
        return;
    }
    if (hasRect() && !pointInRect(maskRect, live)) {
        hideMagnifierPopup();
        return;
    }

    // Adjust stage uses its own topmost popup so the magnifier can cover ToolCap.
    if (cap->stage == WinCap::CapStage::Adjust) {
        updateMagnifierPopup(live);
        return;
    }
    hideMagnifierPopup();

    const float scale = win->dpi;
    const float panelW = 179.f * scale;
    const float panelH = 227.f * scale;
    const float dx = 8.f * scale;
    const float dy = 20.f * scale;
    float left = live.x + dx;
    float top = live.y + dy;
    if (left + panelW > win->w) left = live.x - dx - panelW;
    if (top + panelH > win->h) top = live.y - dy - panelH;
    left = std::clamp(left, 0.f, std::max(0.f, win->w - panelW));
    top = std::clamp(top, 0.f, std::max(0.f, win->h - panelH));
    paintMagnifierPanel(ctx, live, left, top);
}
'''

pattern = re.compile(r'void CutMask::paintMagnifier\(ID2D1DeviceContext\* ctx\)\n\{.*?\n\}\n\nvoid CutMask::paintHelp', re.S)
if not pattern.search(cpp):
    raise SystemExit('paintMagnifier block not found')
cpp = pattern.sub(magnifier_block + '\nvoid CutMask::paintHelp', cpp, count=1)

help_block = r'''void CutMask::paintHelp(ID2D1DeviceContext* ctx)
{
    if (!ctx || hideLabel) return;
    auto* cap = static_cast<WinCap*>(win);
    if (!cap || cap->stage != WinCap::CapStage::Select) return;

    const float scale = win->dpi;
    const bool dragging = cap->isPress;
    const float panelW = (dragging ? 380.f : 405.f) * scale;
    const float panelH = (dragging ? 124.f : 226.f) * scale;

    // Use the monitor work area rather than the fullscreen capture window so
    // this helper always sits above a bottom taskbar (and respects side taskbars).
    POINT cursorScreen{};
    GetCursorPos(&cursorScreen);
    HMONITOR monitor = MonitorFromPoint(cursorScreen, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT work{ win->x, win->y, win->x + (LONG)std::lround(win->w), win->y + (LONG)std::lround(win->h) };
    if (monitor && GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;

    const float workLeft = (float)(work.left - win->x);
    const float workTop = (float)(work.top - win->y);
    const float workRight = (float)(work.right - win->x);
    const float workBottom = (float)(work.bottom - win->y);
    const float margin = 14.f * scale;
    float left = workLeft + margin;
    float top = workBottom - margin - panelH;
    left = std::clamp(left, workLeft, std::max(workLeft, workRight - panelW));
    top = std::clamp(top, workTop, std::max(workTop, workBottom - panelH));

    const auto panel = D2D1::RectF(left, top, left + panelW, top + panelH);
    ctx->FillRectangle(panel, brushHelpBg.Get());
    ctx->DrawRectangle(panel, brushKeyBorder.Get(), std::max(1.f, scale));

    auto d2d = Ling::D2D::get();
    auto drawRow = [&](float rowY, const std::vector<std::wstring>& keys, const std::wstring& desc) {
        float x = left + 13.f * scale;
        for (const auto& key : keys) {
            auto keyLayout = d2d->makeTextLayout(key, 11.5f * scale);
            if (!keyLayout) continue;
            DWRITE_TEXT_METRICS km{};
            keyLayout->GetMetrics(&km);
            const float kw = std::max(21.f * scale, km.width + 9.f * scale);
            const float kh = 21.f * scale;
            ctx->DrawRectangle(D2D1::RectF(x, rowY, x + kw, rowY + kh),
                brushKeyBorder.Get(), std::max(1.f, scale));
            ctx->DrawTextLayout({ x + (kw - km.width) * .5f, rowY + 1.5f * scale },
                keyLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            x += kw + 5.f * scale;
        }
        auto descLayout = d2d->makeTextLayout(desc, 12.f * scale);
        if (descLayout) ctx->DrawTextLayout({ x + 10.f * scale, rowY + 1.5f * scale },
            descLayout.Get(), brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    };

    const float firstY = top + 14.f * scale;
    const float step = 34.f * scale;
    drawRow(firstY + step * 0, { L"W",L"A",L"S",L"D" }, L"移动鼠标指针 1 像素");
    drawRow(firstY + step * 1, { L"Tab" }, L"切换窗口 / 界面元素检测");
    drawRow(firstY + step * 2, { L"Ctrl",L"A" }, L"当前屏幕 / 全屏");
    if (!dragging) {
        drawRow(firstY + step * 3, { L"R",L"Shift",L"R" }, L"使用上一次截图区域");
        drawRow(firstY + step * 4, { L",",L"." }, L"回溯截图区域历史");
        drawRow(firstY + step * 5, { L"C",L"Shift" }, L"复制颜色 / 切换 RGB/HEX");
    }
}
'''

help_pattern = re.compile(r'void CutMask::paintHelp\(ID2D1DeviceContext\* ctx\)\n\{.*?\n\}\n\nvoid CutMask::suppressLegacyMagnifier', re.S)
if not help_pattern.search(cpp):
    raise SystemExit('paintHelp block not found')
cpp = help_pattern.sub(help_block + '\nvoid CutMask::suppressLegacyMagnifier', cpp, count=1)

CPP.write_text(cpp, encoding='utf-8')

wincap = WINCAP.read_text(encoding='utf-8-sig')
old = '''        stage = CapStage::Adjust;
        refresh();  // 收掉放大镜
        makeToolCap();'''
new = '''        stage = CapStage::Adjust;
        refresh();
        makeToolCap();
        // ToolCap is a separate topmost window. Raise the magnifier after it is
        // created so the magnifier can visually cover the toolbar when they overlap.
        cutMask->syncMagnifier(pos);'''
if old not in wincap:
    if 'cutMask->syncMagnifier(pos);' not in wincap:
        raise SystemExit('WinCap Adjust transition marker not found')
else:
    wincap = wincap.replace(old, new, 1)
WINCAP.write_text(wincap, encoding='utf-8')

print('capture UX V4 patch applied')
