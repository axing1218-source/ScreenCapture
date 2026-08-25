#pragma once

#include <include/Ling.h>
#include <robuffer.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <thread>
#include <atomic>
#include "Win/WinCap.h"
#include "Win/CutMask.h"

// WeShot integrated OCR layer.
//
// The upstream project launches an external ImageReader.exe and closes the capture window.
// We keep the capture session alive, run Windows OCR locally, and attach a selectable text
// panel to the existing full-screen WinCap body. This keeps OCR as part of the screenshot UI.
namespace WeShotOcr
{
    struct WordBlock
    {
        std::wstring text;
        float x{ 0 }, y{ 0 }, w{ 0 }, h{ 0 };
    };

    struct Result
    {
        std::wstring text;
        std::vector<WordBlock> words;
        std::wstring error;
    };

    struct State
    {
        WinCap* win{ nullptr };
        Ling::Node* panel{ nullptr };
        Ling::Label* title{ nullptr };
        Ling::Label* status{ nullptr };
        Ling::TextBox* textBox{ nullptr };
        Ling::Button* copyBtn{ nullptr };
        Ling::Button* closeBtn{ nullptr };
        std::vector<WordBlock> words;
        std::atomic<unsigned long long> requestId{ 0 };
        bool busy{ false };
    };

    inline State state;

    inline void layoutPanel()
    {
        if (!state.win || !state.panel || !state.win->cutMask || !state.win->cutMask->hasRect()) return;

        const auto& r = state.win->cutMask->maskRect;
        const float dpi = (std::max)(0.5f, state.win->dpi);
        const float deskW = state.win->w / dpi;
        const float deskH = state.win->h / dpi;
        const float left = r.left / dpi;
        const float top = r.top / dpi;
        const float right = r.right / dpi;
        const float bottom = r.bottom / dpi;

        constexpr float gap = 8.f;
        constexpr float panelW = 330.f;
        float panelH = (std::clamp)(bottom - top, 220.f, 520.f);
        panelH = (std::min)(panelH, (std::max)(120.f, deskH - 16.f));

        float px = right + gap;
        if (px + panelW > deskW - 8.f) {
            px = left - gap - panelW;
        }
        if (px < 8.f) {
            // Narrow/full-screen selection: overlay the panel near the selection's right edge.
            px = (std::max)(8.f, (std::min)(deskW - panelW - 8.f, right - panelW - 8.f));
        }

        float py = top;
        if (py + panelH > deskH - 8.f) py = deskH - panelH - 8.f;
        py = (std::max)(8.f, py);

        state.panel->setSize(panelW, panelH);
        state.panel->setPosition(Ling::Edge::Left, px);
        state.panel->setPosition(Ling::Edge::Top, py);
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

    inline Result recognize(std::vector<BYTE> pixels, int width, int height)
    {
        Result result;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

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
                    for (auto const& word : line.Words()) {
                        auto rc = word.BoundingRect();
                        result.words.push_back({ word.Text().c_str(), rc.X, rc.Y, rc.Width, rc.Height });
                    }
                }
                if (result.text.empty()) result.error = L"没有识别到文字。";
            }
            winrt::uninit_apartment();
        }
        catch (const winrt::hresult_error& e) {
            result.error = std::wstring(L"OCR 失败：") + e.message().c_str();
            try { winrt::uninit_apartment(); } catch (...) {}
        }
        catch (...) {
            result.error = L"OCR 失败：发生未知错误。";
            try { winrt::uninit_apartment(); } catch (...) {}
        }
        return result;
    }

    inline void ensurePanel(WinCap* win)
    {
        if (!win) return;
        if (state.win == win && state.panel) {
            state.panel->show();
            layoutPanel();
            return;
        }

        state.win = win;
        state.panel = win->body->makeChild<Ling::Node>();
        state.panel->setPositionType(Ling::Position::Absolute);
        state.panel->setFlexDirection(Ling::FlexDirection::Column);
        state.panel->setPadding(12.f);
        state.panel->setBg(0xFFFFFFFF);
        state.panel->setBorder(1.f, 0xD4D4D4FF);
        state.panel->setBorderRadius(8.f);

        auto header = state.panel->makeChild<Ling::Node>();
        header->setWidthPercent(100.f);
        header->setHeight(30.f);
        header->setFlexShrink(0.f);
        header->setFlexDirection(Ling::FlexDirection::Row);
        header->setAlignItems(Ling::Align::Center);

        state.title = header->makeChild<Ling::Label>();
        state.title->setText(L"文字识别");
        state.title->setFontSize(15.f);
        state.title->setColor(0x222222FF);
        state.title->setFlexGrow(1.f);

        state.copyBtn = header->makeChild<Ling::Button>();
        state.copyBtn->setText(L"复制全部");
        state.copyBtn->setSize(68.f, 26.f);
        state.copyBtn->setFontSize(12.f);
        state.copyBtn->setColor(0x333333FF);
        state.copyBtn->setBg(0xF5F5F5FF);
        state.copyBtn->setHoverBg(0xEBEBEBFF);
        state.copyBtn->setBorderRadius(5.f);
        state.copyBtn->setMarginRight(6.f);
        state.copyBtn->onClick.add([](Ling::Button*) {
            if (state.textBox) Ling::Util::setTextToClipboard(state.textBox->getText());
        });

        state.closeBtn = header->makeChild<Ling::Button>();
        state.closeBtn->setText(L"×");
        state.closeBtn->setSize(26.f, 26.f);
        state.closeBtn->setFontSize(17.f);
        state.closeBtn->setColor(0x666666FF);
        state.closeBtn->setHoverBg(0xF2F2F2FF);
        state.closeBtn->setBorderRadius(5.f);
        state.closeBtn->onClick.add([](Ling::Button*) {
            if (state.panel) state.panel->hide();
        });

        state.status = state.panel->makeChild<Ling::Label>();
        state.status->setText(L"");
        state.status->setFontSize(12.f);
        state.status->setColor(0x888888FF);
        state.status->setMarginTop(4.f);
        state.status->setMarginBottom(6.f);

        state.textBox = state.panel->makeChild<Ling::TextBox>();
        state.textBox->setWidthPercent(100.f);
        state.textBox->setFlexGrow(1.f);
        state.textBox->setFontSize(14.f);
        state.textBox->setPadding(9.f);
        state.textBox->setBg(0xFAFAFAFF);
        state.textBox->setBorder(1.f, 0xE3E3E3FF);
        state.textBox->setBorderRadius(6.f);
        state.textBox->setSelectionBgColor(0xB8DDF799);

        win->onMouseMove.add([](POINT) {
            if (state.panel) layoutPanel();
        });
        win->onDpiChanged.add([]() {
            if (state.panel) layoutPanel();
        });
        win->onDestroy.add([]() {
            state.requestId.fetch_add(1);
            state.win = nullptr;
            state.panel = nullptr;
            state.title = nullptr;
            state.status = nullptr;
            state.textBox = nullptr;
            state.copyBtn = nullptr;
            state.closeBtn = nullptr;
            state.words.clear();
            state.busy = false;
        });

        layoutPanel();
        state.panel->show();
        win->refresh();
    }

    inline void show(WinCap* win)
    {
        if (!win || !win->cutMask || !win->cutMask->hasRect()) return;
        ensurePanel(win);
        if (!state.textBox || state.busy) return;

        std::vector<BYTE> pixels;
        int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) {
            state.status->setText(L"读取截图失败");
            state.textBox->setText(L"");
            return;
        }

        state.busy = true;
        state.status->setText(L"正在识别...");
        state.textBox->setText(L"");
        const auto myRequest = ++state.requestId;

        std::thread([pixels = std::move(pixels), width, height, myRequest, win]() mutable {
            auto result = recognize(std::move(pixels), width, height);
            Ling::App::get()->dq.TryEnqueue([result = std::move(result), myRequest, win]() mutable {
                if (state.win != win || state.requestId.load() != myRequest || !state.textBox) return;
                state.busy = false;
                state.words = std::move(result.words);
                if (!result.error.empty()) {
                    state.status->setText(result.error);
                    state.textBox->setText(L"");
                }
                else {
                    state.status->setText(std::format(L"已识别 {} 个文字块", state.words.size()));
                    state.textBox->setText(result.text);
                }
                if (state.panel) state.panel->show();
            });
        }).detach();
    }
}
