#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <wrl.h>
#include "WinPin.h"
#include "../History.h"

// Bitmap transforms for pinned screenshots.  Before a transform we flatten the current
// annotation stack into the bitmap, then transform that composite.  This keeps the
// result visually exact and avoids trying to rotate every individual Shape type.
struct StarCapPinTransform
{
    static bool rotateClockwise(WinPin* win)
    {
        if (!win) return false;
        std::vector<BYTE> src;
        D2D1_SIZE_U size{};
        if (!win->getImagePixels(src, size) || !size.width || !size.height) return false;

        const UINT32 srcW = size.width, srcH = size.height;
        const UINT32 dstW = srcH, dstH = srcW;
        std::vector<BYTE> dst((size_t)dstW * dstH * 4);
        for (UINT32 y = 0; y < srcH; ++y) {
            for (UINT32 x = 0; x < srcW; ++x) {
                const UINT32 dx = srcH - 1 - y;
                const UINT32 dy = x;
                const BYTE* s = src.data() + ((size_t)y * srcW + x) * 4;
                BYTE* d = dst.data() + ((size_t)dy * dstW + dx) * 4;
                memcpy(d, s, 4);
            }
        }
        return replaceBitmap(win, dst, dstW, dstH);
    }

    static bool mirrorHorizontal(WinPin* win)
    {
        if (!win) return false;
        std::vector<BYTE> src;
        D2D1_SIZE_U size{};
        if (!win->getImagePixels(src, size) || !size.width || !size.height) return false;

        const UINT32 w = size.width, h = size.height;
        std::vector<BYTE> dst(src.size());
        for (UINT32 y = 0; y < h; ++y) {
            for (UINT32 x = 0; x < w; ++x) {
                const BYTE* s = src.data() + ((size_t)y * w + x) * 4;
                BYTE* d = dst.data() + ((size_t)y * w + (w - 1 - x)) * 4;
                memcpy(d, s, 4);
            }
        }
        return replaceBitmap(win, dst, w, h);
    }

private:
    static bool replaceBitmap(WinPin* win, const std::vector<BYTE>& pixels, UINT32 width, UINT32 height)
    {
        if (!win || pixels.empty() || !width || !height) return false;
        auto d2d = Ling::D2D::get();
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
        props.dpiX = 96.f;
        props.dpiY = 96.f;

        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        if (FAILED(d2d->deviceContext->CreateBitmap(
            D2D1::SizeU(width, height), pixels.data(), width * 4, &props, bitmap.GetAddressOf()))) {
            return false;
        }

        const int centerX = win->x + (int)std::lround(win->w * .5f);
        const int centerY = win->y + (int)std::lround(win->h * .5f);
        win->screenImg = std::move(bitmap);
        win->history = std::make_unique<History>(win);
        win->shapeHover = nullptr;
        win->newShape = nullptr;
        win->editingText = nullptr;
        if (win->textBox) win->textBox->hide();
        win->isMouseDown = false;
        win->prevPressCreatedShape = false;

        win->applyWinSize();
        win->setPosition(centerX - (int)std::lround(win->w * .5f),
                         centerY - (int)std::lround(win->h * .5f));
        win->layoutTools();
        win->refresh();
        return true;
    }
};
