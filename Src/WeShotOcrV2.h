#pragma once

#include <include/Ling.h>
#include <robuffer.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include "Win/WinCap.h"
#include "Win/CutMask.h"
#include "Util.h"

// WeShot OCR result flow.
//
// OCR is intentionally kept lightweight here. The capture overlay is closed as soon as the
// user chooses OCR, then a normal standalone result window opens. The left side keeps the
// captured image; the right side is a real Ling::TextBox so mouse drag selection and Ctrl+C
// do not compete with WinCap's selection/resize mouse handling.
namespace WeShotOcrV2
{
    struct Result
    {
        std::wstring text;
        std::wstring error;
    };

    class OcrResultWindow;
    inline OcrResultWindow* activeWindow{ nullptr };
    inline std::atomic<unsigned long long> requestId{ 0 };

    inline bool copyTextReliable(HWND owner, const std::wstring& text)
    {
        if (text.empty()) return false;
        bool opened = false;
        for (int i = 0; i < 8; ++i) {
            if (OpenClipboard(owner)) { opened = true; break; }
            Sleep(8);
        }
        if (!opened) return false;

        bool ok = false;
        if (EmptyClipboard()) {
            const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (mem) {
                auto dst = static_cast<wchar_t*>(GlobalLock(mem));
                if (dst) {
                    memcpy(dst, text.c_str(), text.size() * sizeof(wchar_t));
                    dst[text.size()] = L'\0';
                    GlobalUnlock(mem);
                    if (SetClipboardData(CF_UNICODETEXT, mem)) {
                        ok = true;
                        mem = nullptr;
                    }
                }
                if (mem) GlobalFree(mem);
            }
        }
        CloseClipboard();
        return ok;
    }

    inline bool copyCutPixels(WinCap* win, std::vector<BYTE>& pixels, int& outW, int& outH)
    {
        if (!win) return false;
        auto img = win->getCutImg();
        if (!img) return false;

        const auto size = img->GetPixelSize();
        if (!size.width || !size.height) return false;
        outW = static_cast<int>(size.width);
        outH = static_cast<int>(size.height);

        D2D1_BITMAP_PROPERTIES1 prop{};
        prop.pixelFormat = img->GetPixelFormat();
        prop.dpiX = 96.f;
        prop.dpiY = 96.f;
        prop.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

        Microsoft::WRL::ComPtr<ID2D1Bitmap1> cpu;
        auto ctx = Ling::D2D::get()->deviceContext;
        auto hr = ctx->CreateBitmap(size, nullptr, 0, &prop, cpu.GetAddressOf());
        if (FAILED(hr) || !cpu) return false;
        hr = cpu->CopyFromBitmap(nullptr, img.Get(), nullptr);
        if (FAILED(hr)) return false;

        D2D1_MAPPED_RECT mapped{};
        hr = cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped);
        if (FAILED(hr) || !mapped.bits) return false;

        const size_t rowBytes = static_cast<size_t>(outW) * 4;
        pixels.resize(rowBytes * static_cast<size_t>(outH));
        for (int row = 0; row < outH; ++row) {
            memcpy(pixels.data() + static_cast<size_t>(row) * rowBytes,
                mapped.bits + static_cast<size_t>(row) * mapped.pitch, rowBytes);
        }
        cpu->Unmap();
        return true;
    }

    inline Result recognizeWindows(std::vector<BYTE> pixels, int width, int height)
    {
        Result result;
        bool apartmentReady = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentReady = true;

            using namespace winrt::Windows::Storage::Streams;
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;

            const uint32_t byteCount = static_cast<uint32_t>(pixels.size());
            Buffer buffer(byteCount);
            buffer.Length(byteCount);
            auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
            BYTE* dst{ nullptr };
            winrt::check_hresult(byteAccess->Buffer(&dst));
            memcpy(dst, pixels.data(), pixels.size());

            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);
            bitmap.CopyFromBuffer(buffer);

            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                result.error = L"当前 Windows 没有可用的 OCR 语言包。";
            }
            else {
                auto ocr = engine.RecognizeAsync(bitmap).get();
                bool firstLine = true;
                for (auto const& line : ocr.Lines()) {
                    if (!firstLine) result.text += L"\r\n";
                    firstLine = false;
                    result.text += line.Text().c_str();
                }
                if (result.text.empty()) result.error = L"没有识别到文字。";
            }

            winrt::uninit_apartment();
            apartmentReady = false;
        }
        catch (const winrt::hresult_error& e) {
            result.error = std::wstring(L"OCR 失败：") + e.message().c_str();
            if (apartmentReady) { try { winrt::uninit_apartment(); } catch (...) {} }
        }
        catch (...) {
            result.error = L"OCR 失败：发生未知错误。";
            if (apartmentReady) { try { winrt::uninit_apartment(); } catch (...) {} }
        }
        return result;
    }

    class OcrResultWindow : public Ling::WinBase
    {
    public:
        OcrResultWindow(std::vector<BYTE> data, int imgW, int imgH)
            : pixels(std::move(data)), imageW(imgW), imageH(imgH)
        {
            setTitle(L"WeShot - 文字识别");
            setSize(1080.f, 680.f);
            setMinSize(760.f, 440.f);
            setCenter();

            onMouseDown.add([this](POINT pos, bool isRight) {
                if (isRight && imageCanvas && imageCanvas->isPosIn(pos)) {
                    showImageContextMenu();
                }
            });
            onSizeChanged.add([this]() { refresh(); });
            onDestroy.add([this]() {
                ++requestId;
                if (activeWindow == this) activeWindow = nullptr;
                auto dying = this;
                Ling::App::get()->dq.TryEnqueue([dying]() {
                    delete dying;
                    auto app = Ling::App::get();
                    auto it = app->args.find(L"--auto-quit");
                    if (it != app->args.end() && it->second == L"true") app->quit(0);
                });
            });
        }

        void open()
        {
            createNativeWindow(0, WS_OVERLAPPEDWINDOW);
        }

        void setOcrResult(Result result)
        {
            if (!textBox || !status) return;
            if (!result.error.empty()) {
                status->setText(L"识别完成");
                textBox->setPlaceholder(L"");
                textBox->setText(result.error);
                fullText.clear();
            }
            else {
                fullText = std::move(result.text);
                status->setText(L"可直接用鼠标拖选文字，按 Ctrl+C 复制");
                textBox->setPlaceholder(L"");
                textBox->setText(fullText);
            }
            refresh();
        }

    protected:
        void onCreated() override
        {
            body->setBg(0xF4F4F4FF);
            body->setFlexDirection(Ling::FlexDirection::Row);

            imageCanvas = body->makeChild<Ling::Canvas>();
            imageCanvas->setFlexGrow(1.f);
            imageCanvas->setHeightPercent(100.f);
            imageCanvas->setBg(0xF2F2F2FF);

            auto divider = body->makeChild<Ling::Node>();
            divider->setWidth(1.f);
            divider->setHeightPercent(100.f);
            divider->setBg(0xD9D9D9FF);

            auto right = body->makeChild<Ling::Node>();
            right->setWidth(380.f);
            right->setHeightPercent(100.f);
            right->setPadding(12.f);
            right->setBg(0xFFFFFFFF);
            right->setFlexDirection(Ling::FlexDirection::Column);

            auto header = right->makeChild<Ling::Node>();
            header->setHeight(38.f);
            header->setWidthPercent(100.f);
            header->setFlexDirection(Ling::FlexDirection::Row);
            header->setAlignItems(Ling::Align::Center);

            auto title = header->makeChild<Ling::Label>();
            title->setText(L"识别文字");
            title->setFontSize(16.f);
            title->setColor(0x222222FF);
            title->setFlexGrow(1.f);

            auto copyAll = header->makeChild<Ling::Button>();
            copyAll->setText(L"复制全部");
            copyAll->setSize(76.f, 28.f);
            copyAll->setFontSize(12.f);
            copyAll->setBg(0xF3F3F3FF);
            copyAll->setHoverBg(0xE9E9E9FF);
            copyAll->setBorder(1.f, 0xDDDDDDFF);
            copyAll->setBorderRadius(4.f);
            copyAll->onClick.add([this](Ling::Button*) {
                if (copyTextReliable(hwnd, fullText)) status->setText(L"已复制全部文字");
                else if (!fullText.empty()) status->setText(L"复制失败，请重试");
            });

            status = right->makeChild<Ling::Label>();
            status->setHeight(28.f);
            status->setWidthPercent(100.f);
            status->setText(L"正在识别... 左侧图片可右键复制或另存为");
            status->setFontSize(12.f);
            status->setColor(0x777777FF);

            textBox = right->makeChild<Ling::TextBox>();
            textBox->setFlexGrow(1.f);
            textBox->setWidthPercent(100.f);
            textBox->setFontSize(14.f);
            textBox->setPadding(10.f);
            textBox->setBg(0xFAFAFAFF);
            textBox->setBorder(1.f, 0xE1E1E1FF);
            textBox->setBorderRadius(4.f);
            textBox->setSelectionBgColor(0xB8DDF799);
            textBox->setPlaceholder(L"正在识别...");

            if (imageW > 0 && imageH > 0 && !pixels.empty()) {
                D2D1_BITMAP_PROPERTIES1 props{};
                props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
                props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
                props.dpiX = 96.f;
                props.dpiY = 96.f;
                Ling::D2D::get()->deviceContext->CreateBitmap(
                    D2D1::SizeU((UINT32)imageW, (UINT32)imageH), pixels.data(), (UINT32)imageW * 4,
                    &props, imageBitmap.GetAddressOf());
            }

            show();
            SetForegroundWindow(hwnd);
        }

        void layout() override
        {
            Ling::WinBase::layout();
            paintImage();
        }

        LRESULT onHitTest(const POINT pos) override
        {
            return DefWindowProcW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pos.x, pos.y));
        }

    private:
        void paintImage()
        {
            if (!imageCanvas) return;
            auto ctx = imageCanvas->startPaint();
            if (!ctx) return;
            ctx->Clear(D2D1::ColorF(0xF2F2F2));

            if (imageBitmap && imageW > 0 && imageH > 0) {
                const float cw = imageCanvas->w;
                const float ch = imageCanvas->h;
                const float margin = 18.f * dpi;
                const float availW = (std::max)(1.f, cw - margin * 2.f);
                const float availH = (std::max)(1.f, ch - margin * 2.f);
                float scale = (std::min)(availW / imageW, availH / imageH);
                scale = (std::min)(1.f, (std::max)(0.01f, scale));
                const float dw = imageW * scale;
                const float dh = imageH * scale;
                const float left = (cw - dw) * 0.5f;
                const float top = (ch - dh) * 0.5f;
                auto dest = D2D1::RectF(left, top, left + dw, top + dh);
                ctx->DrawBitmap(imageBitmap.Get(), dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }

            imageCanvas->finishPaint();
        }

        void showImageContextMenu()
        {
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            AppendMenuW(menu, MF_STRING, 1, L"复制图片");
            AppendMenuW(menu, MF_STRING, 2, L"另存为 PNG...");

            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            const UINT cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd, WM_NULL, 0, 0);

            if (cmd == 1) {
                if (!pixels.empty()) Util::saveToClipboard(imageW, imageH, pixels.data());
            }
            else if (cmd == 2) {
                auto path = Util::getSaveFilePath(hwnd, L"png");
                if (!path.empty() && !pixels.empty()) Util::saveToFile(path, imageW, imageH, pixels.data());
            }
        }

    private:
        std::vector<BYTE> pixels;
        int imageW{ 0 }, imageH{ 0 };
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap;
        Ling::Canvas* imageCanvas{ nullptr };
        Ling::TextBox* textBox{ nullptr };
        Ling::Label* status{ nullptr };
        std::wstring fullText;
    };

    inline bool containsPoint(POINT)
    {
        return false;
    }

    inline bool hasWindow()
    {
        return activeWindow != nullptr;
    }

    inline void show(WinCap* win)
    {
        if (!win || !win->cutMask || !win->cutMask->hasRect()) return;

        std::vector<BYTE> pixels;
        int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) return;

        const auto myRequest = ++requestId;

        if (activeWindow) activeWindow->close();
        activeWindow = new OcrResultWindow(pixels, width, height);
        activeWindow->open();

        // Result window is independent; finish screenshot mode immediately.
        win->close();

        std::thread([pixels = std::move(pixels), width, height, myRequest]() mutable {
            auto result = recognizeWindows(std::move(pixels), width, height);
            Ling::App::get()->dq.TryEnqueue([result = std::move(result), myRequest]() mutable {
                if (requestId.load() != myRequest || !activeWindow) return;
                activeWindow->setOcrResult(std::move(result));
            });
        }).detach();
    }
}
