#include "pch.h"
#include <include/Ling.h>
#include "CapLong.h"
#include "WinCap.h"
#include "CutMask.h"
#include "WinPin.h"
#include "../Tool/ToolLong.h"
#include "../App.h"
#include "../Util.h"
#include "../Lang.h"
using namespace Microsoft::WRL;

namespace {
    constexpr UINT scrollMsgId = 18;
    constexpr UINT scrollEndMsgId = 19;
    constexpr UINT manualCaptureMsgId = 20;
    constexpr int comparisonH = 100;
    constexpr int maxDismissTime = 8;
    constexpr int scrollSettleMs = 250;
    constexpr int settleRecheckMs = 250;
    constexpr int maxSettleRecheck = 2;
    constexpr int manualPollMs = 120;
    constexpr double bottomMatchMinRatio = 0.9;
    constexpr double bottomMatchMaxError = 40000;

    std::vector<BYTE> toGrayscale(const BYTE* bgra, int width, int height, int stride)
    {
        std::vector<BYTE> gray((size_t)width * height);
        for (int y = 0; y < height; y++) {
            const BYTE* src = bgra + (size_t)y * stride;
            BYTE* dst = gray.data() + (size_t)y * width;
            for (int x = 0; x < width; x++) {
                dst[x] = (BYTE)((src[x * 4] * 114 + src[x * 4 + 1] * 587 + src[x * 4 + 2] * 299) / 1000);
            }
        }
        return gray;
    }

    int findMostSimilarY(const BYTE* gray1, int gray1H, const BYTE* gray2, int gray2H, int width)
    {
        int searchH = gray1H - gray2H + 1;
        if (searchH <= 0) return 0;
        double minAvgError = DBL_MAX;
        int bestY = 0;
        for (int y = 0; y < searchH; y++) {
            double error = 0.0;
            for (int row = 0; row < gray2H; row++) {
                const BYTE* row1 = gray1 + (size_t)(y + row) * width;
                const BYTE* row2 = gray2 + (size_t)row * width;
                for (int x = 0; x < width; x++) {
                    int diff = (int)row1[x] - (int)row2[x];
                    error += diff * diff;
                }
            }
            double avgError = error / gray2H;
            if (avgError < minAvgError) {
                minAvgError = avgError;
                bestY = y;
            }
        }
        return bestY;
    }

    bool framesDiffer(const std::vector<BYTE>& a, const std::vector<BYTE>& b)
    {
        if (a.size() != b.size()) return true;
        return memcmp(a.data(), b.data(), a.size()) != 0;
    }

    int findScrollByBottomStrip(const BYTE* grayOld, const BYTE* grayNew, int width, int stripH)
    {
        double minAvgError = DBL_MAX;
        double avgAtZero = DBL_MAX;
        int bestS = 0;
        for (int s = 0; s < stripH; s++) {
            int rows = stripH - s;
            double error = 0.0;
            for (int r = 0; r < rows; r++) {
                const BYTE* row1 = grayOld + (size_t)(s + r) * width;
                const BYTE* row2 = grayNew + (size_t)r * width;
                for (int x = 0; x < width; x++) {
                    int diff = (int)row1[x] - (int)row2[x];
                    error += diff * diff;
                }
            }
            double avgError = error / rows;
            if (s == 0) avgAtZero = avgError;
            if (avgError < minAvgError) {
                minAvgError = avgError;
                bestS = s;
            }
        }
        if (bestS <= 0) return 0;
        if (minAvgError >= avgAtZero * bottomMatchMinRatio) return 0;
        if (minAvgError > bottomMatchMaxError) return 0;
        return bestS;
    }
}

CapLong::CapLong(WinCap* win) : win(win)
{
    auto d2d = Ling::D2D::get();
    d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), textBrush.GetAddressOf());
    d2d->deviceContext->CreateSolidColorBrush(D2D1::ColorF(0x000000, 0.68f), bgBrush.GetAddressOf());

    // Clicking "long screenshot" now starts immediately. The selected area becomes a hole so the
    // underlying app receives the user's wheel gestures; we poll the pixels and stitch only when
    // the user actually scrolls. Automatic scrolling remains an optional toolbar action.
    isCapturing = true;
    win->hollowWin();
    makeTool();
    firstStep();
}

CapLong::~CapLong()
{
}

void CapLong::dispose()
{
    win->killTimer(scrollMsgId);
    win->killTimer(scrollEndMsgId);
    win->killTimer(manualCaptureMsgId);
    if (tool) tool->close();
}

void CapLong::paint(ID2D1DeviceContext* ctx)
{
    paintImgPreview(ctx);
    if (isFinish && layoutTextEnd) {
        auto borderRadius{ 4.f * win->dpi };
        ctx->FillRoundedRectangle(D2D1::RoundedRect(stopTextRect, borderRadius, borderRadius), bgBrush.Get());
        ctx->DrawTextLayout(stopTextPos, layoutTextEnd.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
}

void CapLong::setCursor()
{
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void CapLong::onMove(POINT)
{
}

void CapLong::onUp(POINT)
{
}

void CapLong::scheduleNextCapture(int delayMs)
{
    if (!isCapturing || isFinish || autoScroll) return;
    win->setTimer(delayMs, manualCaptureMsgId);
}

void CapLong::onTimerCB(UINT timerId)
{
    if (timerId == manualCaptureMsgId) {
        win->killTimer(manualCaptureMsgId);
        if (!isCapturing || isFinish || autoScroll) return;
        // The first timer tick happens after CapLong has been assigned into WinCap, so this also
        // makes the initial preview visible even though firstStep ran inside the constructor.
        win->refresh();
        capStep();
    }
    else if (timerId == scrollMsgId) {
        win->killTimer(scrollMsgId);
        if (!isCapturing || isFinish || !autoScroll) return;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = -WHEEL_DELTA;
        SendInput(1, &input, sizeof(INPUT));
        win->setTimer(scrollSettleMs, scrollEndMsgId);
    }
    else if (timerId == scrollEndMsgId) {
        win->killTimer(scrollEndMsgId);
        if (!isCapturing || isFinish) return;
        capStep();
    }
}

void CapLong::firstStep()
{
    auto& maskRect = win->cutMask->maskRect;
    imgW = int(maskRect.right - maskRect.left);
    imgH = int(maskRect.bottom - maskRect.top);
    resultH = imgH;
    capStartPos.x = (int)maskRect.left;
    capStartPos.y = (int)maskRect.top;
    ClientToScreen(win->hwnd, &capStartPos);
    imgData = Util::captureScreen(capStartPos.x, capStartPos.y, imgW, imgH);
    img1 = imgData;
    makeImgPreview();
    scheduleNextCapture(manualPollMs);
}

void CapLong::makeImgPreview()
{
    imgPreview.Reset();
    if (imgW <= 0 || resultH <= 0 || imgData.empty()) return;
    float previewScaleW = tool ? (float)tool->w / (float)imgW : 1.0f;
    int previewW = (int)((float)imgW * previewScaleW);
    int previewH = (int)((float)resultH * previewScaleW);
    if (previewW <= 0 || previewH <= 0) return;

    std::vector<BYTE> scaledData((size_t)previewW * 4 * previewH);
    for (int y = 0; y < previewH; y++) {
        int srcY = (int)((float)y / previewScaleW);
        if (srcY >= resultH) srcY = resultH - 1;
        for (int x = 0; x < previewW; x++) {
            int srcX = (int)((float)x / previewScaleW);
            if (srcX >= imgW) srcX = imgW - 1;
            int srcIdx = (srcY * imgW + srcX) * 4;
            int dstIdx = (y * previewW + x) * 4;
            scaledData[dstIdx] = imgData[srcIdx];
            scaledData[dstIdx + 1] = imgData[srcIdx + 1];
            scaledData[dstIdx + 2] = imgData[srcIdx + 2];
            scaledData[dstIdx + 3] = imgData[srcIdx + 3];
        }
    }
    D2D1_BITMAP_PROPERTIES1 props = {
        .pixelFormat{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)},
        .dpiX{96.0f}, .dpiY{96.0f}, .bitmapOptions{D2D1_BITMAP_OPTIONS_NONE}
    };
    Ling::D2D::get()->deviceContext->CreateBitmap(D2D1::SizeU(previewW, previewH), scaledData.data(), previewW * 4, props, imgPreview.GetAddressOf());
}

void CapLong::capStep()
{
    auto data = Util::captureScreen(capStartPos.x, capStartPos.y, imgW, imgH);
    if (data.empty()) {
        if (autoScroll) win->setTimer(500, scrollMsgId);
        else scheduleNextCapture(manualPollMs);
        return;
    }

    if (firstCheck) {
        changeStartY = -1;
        for (int y = 0; y < imgH; y++) {
            for (int x = 0; x < imgW; x++) {
                int idx = (y * imgW + x) * 4;
                if (img1[idx] != data[idx] || img1[idx + 1] != data[idx + 1] || img1[idx + 2] != data[idx + 2]) {
                    changeStartY = y;
                    break;
                }
            }
            if (changeStartY != -1) break;
        }
        if (changeStartY == -1) {
            if (autoScroll) {
                if (++dismissTime > maxDismissTime) { stopCap(); return; }
                win->setTimer(500, scrollMsgId);
            }
            else {
                scheduleNextCapture(manualPollMs);
            }
            return;
        }
        firstCheck = false;
    }

    int rowPix{ imgW * 4 };
    int stripH = std::min(comparisonH, imgH - changeStartY);
    if (stripH <= 0) {
        if (autoScroll) win->setTimer(500, scrollMsgId);
        else scheduleNextCapture(manualPollMs);
        return;
    }

    int img1StripH = imgH - changeStartY;
    auto gray1 = toGrayscale(img1.data() + (size_t)changeStartY * rowPix, imgW, img1StripH, rowPix);
    auto gray2 = toGrayscale(data.data() + (size_t)changeStartY * rowPix, imgW, stripH, rowPix);
    int y = findMostSimilarY(gray1.data(), img1StripH, gray2.data(), stripH, imgW);
    if (y == 0) {
        auto gray1Bottom = toGrayscale(img1.data() + (size_t)(imgH - stripH) * rowPix, imgW, stripH, rowPix);
        auto gray2Bottom = toGrayscale(data.data() + (size_t)(imgH - stripH) * rowPix, imgW, stripH, rowPix);
        y = findScrollByBottomStrip(gray1Bottom.data(), gray2Bottom.data(), imgW, stripH);
    }

    if (y == 0) {
        if (framesDiffer(data, img1) && settleRecheckCount < maxSettleRecheck) {
            settleRecheckCount++;
            if (autoScroll) win->setTimer(settleRecheckMs, scrollEndMsgId);
            else scheduleNextCapture(settleRecheckMs);
            return;
        }
        settleRecheckCount = 0;
        if (autoScroll) {
            if (++dismissTime > maxDismissTime) { stopCap(); return; }
            win->setTimer(500, scrollMsgId);
        }
        else {
            // Manual mode never decides that the user is "done" merely because they paused.
            scheduleNextCapture(manualPollMs);
        }
        return;
    }

    dismissTime = 0;
    settleRecheckCount = 0;
    int paintStart = resultH - (imgH - y - changeStartY);
    int newResultH = paintStart + (imgH - changeStartY);
    if (paintStart < 0 || newResultH <= 0) {
        if (autoScroll) win->setTimer(500, scrollMsgId);
        else scheduleNextCapture(manualPollMs);
        return;
    }

    std::vector<BYTE> newResult((size_t)rowPix * newResultH);
    CopyMemory(newResult.data(), imgData.data(), imgData.size());
    for (int row = 0; row < imgH - changeStartY; row++) {
        CopyMemory(newResult.data() + (size_t)(paintStart + row) * rowPix,
            data.data() + (size_t)(changeStartY + row) * rowPix, rowPix);
    }
    imgData = std::move(newResult);
    img1 = std::move(data);
    resultH = newResultH;

    if (resultH > 36000) { stopCap(); return; }
    makeImgPreview();
    win->refresh();
    if (autoScroll) win->setTimer(500, scrollMsgId);
    else scheduleNextCapture(manualPollMs);
}

void CapLong::startAutoScroll()
{
    if (!isCapturing || isFinish || autoScroll) return;
    autoScroll = true;
    dismissTime = 0;
    settleRecheckCount = 0;
    win->killTimer(manualCaptureMsgId);

    // Put the pointer over the selected live area so SendInput(WHEEL) is delivered to the page,
    // not to the toolbar button the user just clicked.
    auto& r = win->cutMask->maskRect;
    POINT center{ (LONG)((r.left + r.right) / 2.f), (LONG)((r.top + r.bottom) / 2.f) };
    ClientToScreen(win->hwnd, &center);
    SetCursorPos(center.x, center.y);
    targetHwnd = WindowFromPoint(center);
    win->setTimer(80, scrollMsgId);
}

void CapLong::makeTool()
{
    tool = std::make_unique<ToolLong>(win, this);
    layoutTool();
    tool->createNativeWindow(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, WS_POPUP);
}

void CapLong::layoutTool()
{
    if (!tool) return;
    auto toolW{ tool->w };
    POINT pos{ 0,0 };
    auto& cutMask = win->cutMask;
    if (win->w - cutMask->maskRect.right - 2 * win->dpi < toolW) {
        pos.x = (LONG)(cutMask->maskRect.left - toolW - cutMask->strokeWidth - 2 * win->dpi);
    }
    else {
        pos.x = (LONG)(cutMask->maskRect.right + cutMask->strokeWidth + 2 * win->dpi);
    }
    pos.y = (LONG)(cutMask->maskRect.bottom - tool->h);
    ClientToScreen(win->hwnd, &pos);
    tool->setPosition(pos.x, pos.y);
}

void CapLong::paintImgPreview(ID2D1DeviceContext* ctx)
{
    if (!imgPreview || !tool) return;
    auto bitmapSize = imgPreview->GetPixelSize();
    float drawW = (float)bitmapSize.width;
    float drawH = (float)bitmapSize.height;
    POINT pos{ tool->x, tool->y - (int)drawH - (int)(2 * win->dpi) };
    ScreenToClient(win->hwnd, &pos);
    D2D1_RECT_F destRect = D2D1::RectF((float)pos.x, (float)pos.y, pos.x + drawW, pos.y + drawH);
    ctx->DrawBitmap(imgPreview.Get(), destRect);
}

void CapLong::stopCap()
{
    isFinish = true;
    isCapturing = false;
    autoScroll = false;
    makeStopText();
    win->restoreWin();
    win->killTimer(scrollMsgId);
    win->killTimer(scrollEndMsgId);
    win->killTimer(manualCaptureMsgId);
    win->refresh();
}

void CapLong::makeStopText()
{
    if (resultH > 36000) {
        layoutTextEnd = Ling::D2D::get()->makeTextLayout(Lang::get(L"long.tooLong"), 13 * win->dpi);
    }
    else {
        layoutTextEnd = Ling::D2D::get()->makeTextLayout(Lang::get(L"long.reachedBottom"), 13 * win->dpi);
    }
    if (!layoutTextEnd) return;
    DWRITE_TEXT_METRICS tm{};
    layoutTextEnd->GetMetrics(&tm);
    auto& maskRect = win->cutMask->maskRect;
    auto halfX = maskRect.left + (maskRect.right - maskRect.left) / 2;
    auto halfW = tm.width / 2;
    float padding{ 8 * win->dpi };
    stopTextRect.left = halfX - halfW - padding;
    stopTextRect.top = maskRect.bottom - 30 * win->dpi - padding;
    stopTextRect.right = halfX + halfW + padding;
    stopTextRect.bottom = maskRect.bottom - padding;
    layoutTextEnd->SetMaxWidth(stopTextRect.right - stopTextRect.left);
    layoutTextEnd->SetMaxHeight(stopTextRect.bottom - stopTextRect.top);
    stopTextPos = { halfX - halfW, stopTextRect.top + (stopTextRect.bottom - stopTextRect.top - tm.height) / 2 };
}

void CapLong::copyToClipboard()
{
    if (imgData.empty()) return;
    Util::saveToClipboard(imgW, resultH, imgData.data());
}

bool CapLong::saveToFile()
{
    if (imgData.empty()) return false;
    auto path = Util::getSaveFilePath(win->hwnd);
    if (path.empty()) return false;
    return Util::saveToFile(path, imgW, resultH, imgData.data());
}

void CapLong::pin()
{
    if (imgData.empty()) return;
    auto monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    GetMonitorInfo(monitor, &mi);
    auto& workArea = mi.rcWork;
    int screenW = workArea.right - workArea.left;
    int screenH = workArea.bottom - workArea.top;
    int posX = workArea.left + (screenW - imgW) / 2;
    int posY = workArea.top + (screenH - std::min(resultH, screenH)) / 2;
    WinPin::initFromData(posX, posY, imgW, resultH, imgData);
}
