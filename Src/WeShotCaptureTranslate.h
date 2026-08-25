#pragma once

#include <include/Ling.h>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include "Win/WinCap.h"
#include "Win/CutMask.h"
#include "Setting.h"
#include "GeminiClient.h"

namespace WeShotCaptureTranslate
{
    class LoadingOverlay : public Ling::WinBase
    {
    public:
        LoadingOverlay(int screenX, int screenY, int imageW, int imageH,
            std::vector<BYTE> pixels, float borderWidth)
            : pixels(std::move(pixels)), imageW(imageW), imageH(imageH), borderWidth(borderWidth)
        {
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
        }

        void open()
        {
            createNativeWindow(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, WS_POPUP);
        }

    protected:
        void onCreated() override
        {
            disableBorderRadius();
            canvas = body->makeChild<Ling::Canvas>();
            canvas->enableSwapChain();
            canvas->setSizePercent(100.f, 100.f);
            if (imageW > 0 && imageH > 0 && !pixels.empty()) {
                D2D1_BITMAP_PROPERTIES1 props{};
                props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
                props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
                props.dpiX = 96.f; props.dpiY = 96.f;
                Ling::D2D::get()->deviceContext->CreateBitmap(
                    D2D1::SizeU((UINT32)imageW, (UINT32)imageH), pixels.data(), (UINT32)imageW * 4,
                    &props, imageBitmap.GetAddressOf());
            }
            show();
        }

        void layout() override
        {
            Ling::WinBase::layout();
            if (!canvas) return;
            auto ctx = canvas->startPaint();
            if (!ctx) return;
            ctx->Clear(0);
            auto full = D2D1::RectF(0.f, 0.f, (float)imageW, (float)imageH);
            if (imageBitmap) ctx->DrawBitmap(imageBitmap.Get(), full, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dimBrush, textBrush, borderBrush;
            ctx->CreateSolidColorBrush(D2D1::ColorF(.36f, .36f, .36f, .54f), dimBrush.GetAddressOf());
            ctx->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), textBrush.GetAddressOf());
            if (dimBrush) ctx->FillRectangle(full, dimBrush.Get());

            if (textBrush && imageW > 0 && imageH > 0) {
                float fontSize = std::min(18.f * dpi, std::max(6.f * dpi, imageH * .28f));
                auto tl = Ling::D2D::makeTextLayout(L"正在翻译中...", fontSize,
                    std::max(1.f, (float)imageW), std::max(1.f, (float)imageH));
                if (tl) {
                    tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    ctx->DrawTextLayout({ 0.f, 0.f }, tl.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }

            if (borderWidth > 0.f) {
                ctx->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.5f, 1.f, 0.9f), borderBrush.GetAddressOf());
                if (borderBrush) ctx->DrawRectangle(full, borderBrush.Get(), borderWidth);
            }
            canvas->finishPaint();
        }

        LRESULT onHitTest(const POINT) override { return HTTRANSPARENT; }

    private:
        std::vector<BYTE> pixels;
        int imageW{ 0 }, imageH{ 0 };
        float borderWidth{ 0.f };
        Ling::Canvas* canvas{ nullptr };
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap;
    };

    class TranslationOverlay : public Ling::WinBase
    {
    public:
        TranslationOverlay(int screenX, int screenY, int imageW, int imageH,
            std::vector<BYTE> pixels, std::vector<GeminiClient::TranslationBlock> blocks, float borderWidth)
            : pixels(std::move(pixels)), imageW(imageW), imageH(imageH), blocks(std::move(blocks)), borderWidth(borderWidth)
        {
            x = screenX; y = screenY; w = (float)imageW; h = (float)imageH;
            disableWinAnimation();
        }

        void open()
        {
            createNativeWindow(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, WS_POPUP);
        }

    protected:
        void onCreated() override
        {
            disableBorderRadius();
            canvas = body->makeChild<Ling::Canvas>();
            canvas->enableSwapChain();
            canvas->setSizePercent(100.f, 100.f);
            if (imageW > 0 && imageH > 0 && !pixels.empty()) {
                D2D1_BITMAP_PROPERTIES1 props{};
                props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
                props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
                props.dpiX = 96.f; props.dpiY = 96.f;
                Ling::D2D::get()->deviceContext->CreateBitmap(
                    D2D1::SizeU((UINT32)imageW, (UINT32)imageH), pixels.data(), (UINT32)imageW * 4,
                    &props, imageBitmap.GetAddressOf());
            }
            show();
        }

        void layout() override
        {
            Ling::WinBase::layout();
            if (!canvas) return;
            auto ctx = canvas->startPaint();
            if (!ctx) return;
            ctx->Clear(0);
            if (imageBitmap) {
                auto full = D2D1::RectF(0.f, 0.f, (float)imageW, (float)imageH);
                ctx->DrawBitmap(imageBitmap.Get(), full, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                paintBlocks(ctx, full);
                if (borderWidth > 0.f) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
                    ctx->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.5f, 1.f, 0.9f), border.GetAddressOf());
                    if (border) ctx->DrawRectangle(full, border.Get(), borderWidth);
                }
            }
            canvas->finishPaint();
        }

        LRESULT onHitTest(const POINT) override { return HTTRANSPARENT; }

    private:
        D2D1_COLOR_F sampleBackground(const GeminiClient::TranslationBlock& block) const
        {
            if (pixels.empty()) return D2D1::ColorF(D2D1::ColorF::White);
            int x1 = std::clamp(block.xmin * imageW / 1000, 0, imageW - 1);
            int x2 = std::clamp(block.xmax * imageW / 1000, x1 + 1, imageW);
            int y1 = std::clamp(block.ymin * imageH / 1000, 0, imageH - 1);
            int y2 = std::clamp(block.ymax * imageH / 1000, y1 + 1, imageH);
            int sx = std::max(1, (x2 - x1) / 16), sy = std::max(1, (y2 - y1) / 10);
            unsigned long long r = 0, g = 0, b = 0, n = 0;
            for (int yy = y1; yy < y2; yy += sy) for (int xx = x1; xx < x2; xx += sx) {
                size_t idx = ((size_t)yy * imageW + xx) * 4;
                b += pixels[idx]; g += pixels[idx + 1]; r += pixels[idx + 2]; ++n;
            }
            if (!n) return D2D1::ColorF(D2D1::ColorF::White);
            return D2D1::ColorF((float)r / n / 255.f, (float)g / n / 255.f, (float)b / n / 255.f, .97f);
        }

        void paintBlocks(ID2D1DeviceContext* ctx, const D2D1_RECT_F& imageRect)
        {
            const float dw = imageRect.right - imageRect.left, dh = imageRect.bottom - imageRect.top;
            for (auto& block : blocks) {
                D2D1_RECT_F rect{
                    imageRect.left + dw * block.xmin / 1000.f,
                    imageRect.top + dh * block.ymin / 1000.f,
                    imageRect.left + dw * block.xmax / 1000.f,
                    imageRect.top + dh * block.ymax / 1000.f
                };
                if (rect.right - rect.left < 2.f || rect.bottom - rect.top < 2.f) continue;

                // For a single very short capture, use nearly the full vertical area so the
                // translated line is not clipped just because Gemini returned a tight box.
                if (blocks.size() == 1 && dh < 140.f * dpi) {
                    const float edge = std::min(2.f * dpi, dh * .05f);
                    rect.top = imageRect.top + edge;
                    rect.bottom = imageRect.bottom - edge;
                }

                auto bgColor = sampleBackground(block);
                float lum = bgColor.r * .299f + bgColor.g * .587f + bgColor.b * .114f;
                auto fg = lum > .55f ? D2D1::ColorF(D2D1::ColorF::Black) : D2D1::ColorF(D2D1::ColorF::White);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg, text;
                ctx->CreateSolidColorBrush(bgColor, bg.GetAddressOf());
                ctx->CreateSolidColorBrush(fg, text.GetAddressOf());
                if (!bg || !text) continue;
                ctx->FillRectangle(rect, bg.Get());

                float boxW = std::max(1.f, rect.right - rect.left);
                float boxH = std::max(1.f, rect.bottom - rect.top);
                const float padX = std::min(2.f * dpi, boxW * .05f);
                const float padY = std::min(1.f * dpi, boxH * .04f);
                const float innerW = std::max(1.f, boxW - padX * 2.f);
                const float innerH = std::max(1.f, boxH - padY * 2.f);

                float maxFont = std::min(24.f * dpi, std::max(2.75f * dpi, boxH * .68f));
                float minFont = std::max(2.75f * dpi, std::min(maxFont, boxH * .16f));
                float fontSize = maxFont;
                bool fitted = false;
                const float step = std::max(.25f, .5f * dpi);
                for (float fs = maxFont; fs >= minFont; fs -= step) {
                    auto probe = Ling::D2D::makeTextLayout(block.translation, fs, innerW, 4096.f * dpi);
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
                tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                ctx->DrawTextLayout({ rect.left + padX, rect.top + padY }, tl.Get(), text.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }

        std::vector<BYTE> pixels;
        int imageW{ 0 }, imageH{ 0 };
        std::vector<GeminiClient::TranslationBlock> blocks;
        float borderWidth{ 0.f };
        Ling::Canvas* canvas{ nullptr };
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap;
    };

    inline std::unique_ptr<TranslationOverlay> overlay;
    inline std::unique_ptr<LoadingOverlay> loadingOverlay;
    inline WinCap* owner{ nullptr };
    inline std::atomic<unsigned long long> requestId{ 0 };
    inline bool busy{ false }, ready{ false }, showing{ false }, hooksInstalled{ false };
    inline int cachedX{ 0 }, cachedY{ 0 }, cachedW{ 0 }, cachedH{ 0 };

    inline void closeOverlay()
    {
        if (overlay) { overlay->close(); overlay.reset(); }
        if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
        showing = false;
    }

    inline void reset(WinCap* expected = nullptr)
    {
        if (expected && owner != expected) return;
        ++requestId;
        closeOverlay();
        owner = nullptr; busy = false; ready = false; hooksInstalled = false;
        cachedX = cachedY = cachedW = cachedH = 0;
    }

    inline bool copyCutPixels(WinCap* win, std::vector<BYTE>& pixels, int& outW, int& outH)
    {
        if (!win) return false;
        auto img = win->getCutImg(); if (!img) return false;
        auto size = img->GetPixelSize(); if (!size.width || !size.height) return false;
        outW = (int)size.width; outH = (int)size.height;
        D2D1_BITMAP_PROPERTIES1 prop{};
        prop.pixelFormat = img->GetPixelFormat(); prop.dpiX = 96.f; prop.dpiY = 96.f;
        prop.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> cpu;
        auto ctx = Ling::D2D::get()->deviceContext;
        if (FAILED(ctx->CreateBitmap(size, nullptr, 0, &prop, cpu.GetAddressOf()))) return false;
        if (FAILED(cpu->CopyFromBitmap(nullptr, img.Get(), nullptr))) return false;
        D2D1_MAPPED_RECT mapped{}; if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
        size_t rowBytes = (size_t)outW * 4; pixels.resize(rowBytes * outH);
        for (int row = 0; row < outH; ++row)
            memcpy(pixels.data() + (size_t)row * rowBytes, mapped.bits + (size_t)row * mapped.pitch, rowBytes);
        cpu->Unmap(); return true;
    }

    inline void installHooks(WinCap* win)
    {
        if (hooksInstalled || !win) return;
        hooksInstalled = true;
        win->onDestroy.add([win]() { reset(win); });
        // 用户回到截图本体去拖动/调整选区时先露出原图；下次点“译”若选区已变化会重新请求。
        win->onMouseDown.add([win](POINT, bool isRight) {
            if (owner != win || isRight) return;
            if (busy) {
                ++requestId;
                if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
                busy = false;
            }
            if (showing && overlay) {
                overlay->hide(); showing = false;
            }
        });
    }

    inline void toggle(WinCap* win)
    {
        if (!win || !win->cutMask || !win->cutMask->hasRect()) return;
        if (owner && owner != win) reset();
        if (!owner) owner = win;
        installHooks(win);
        auto& r = win->cutMask->maskRect;
        int sx = win->x + (int)r.left, sy = win->y + (int)r.top;
        int sw = std::max(1, (int)(r.right - r.left)), sh = std::max(1, (int)(r.bottom - r.top));

        if (ready && (sx != cachedX || sy != cachedY || sw != cachedW || sh != cachedH)) {
            ++requestId; closeOverlay(); ready = false; busy = false;
        }
        if (ready && overlay) {
            if (showing) { overlay->hide(); showing = false; }
            else { overlay->show(); showing = true; }
            return;
        }
        if (busy) return;

        auto setting = Setting::get();
        auto apiKey = setting ? setting->getGeminiApiKey() : L"";
        auto model = setting ? setting->getGeminiModel() : L"gemini-3.7-flash";
        if (apiKey.empty()) {
            MessageBoxW(win->hwnd, L"请先在 设置 > 通用设置 中填写并保存 Gemini API Key。",
                L"WeShot 翻译", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::vector<BYTE> pixels; int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) return;
        busy = true;
        cachedX = sx; cachedY = sy; cachedW = width; cachedH = height;
        const auto myRequest = ++requestId;
        const float border = win->cutMask->strokeWidth;

        // Direct screenshot-translation feedback: immediately cover the selected capture with a
        // gray translucent preview and a centered loading message while Gemini is working.
        loadingOverlay = std::make_unique<LoadingOverlay>(sx, sy, width, height, pixels, border);
        loadingOverlay->open();

        auto sourcePixels = pixels;
        std::thread([win, pixels = std::move(pixels), sourcePixels = std::move(sourcePixels), width, height,
            apiKey = std::move(apiKey), model = std::move(model), myRequest, sx, sy, border]() mutable {
            auto result = GeminiClient::translateImage(pixels, width, height, apiKey, model);
            Ling::App::get()->dq.TryEnqueue([win, result = std::move(result), sourcePixels = std::move(sourcePixels),
                width, height, myRequest, sx, sy, border]() mutable {
                if (requestId.load() != myRequest || owner != win || WinCap::get() != win) return;
                if (loadingOverlay) { loadingOverlay->close(); loadingOverlay.reset(); }
                busy = false;
                if (!result.ok) {
                    auto msg = result.error.empty() ? std::wstring(L"Gemini 翻译失败。") : result.error;
                    MessageBoxW(win->hwnd, msg.c_str(), L"WeShot 翻译", MB_OK | MB_ICONWARNING);
                    return;
                }
                overlay = std::make_unique<TranslationOverlay>(sx, sy, width, height,
                    std::move(sourcePixels), std::move(result.blocks), border);
                overlay->open();
                ready = true; showing = true;
            });
        }).detach();
    }
}
