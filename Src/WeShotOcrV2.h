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
#include "WeShotPaddleOcr.h"

namespace WeShotOcrV2
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
        std::wstring engine;
    };

    struct State
    {
        WinCap* win{ nullptr };
        Ling::Node* panel{ nullptr };
        Ling::Node* header{ nullptr };
        Ling::Label* title{ nullptr };
        Ling::Label* status{ nullptr };
        Ling::TextBox* textBox{ nullptr };
        Ling::Button* copyBtn{ nullptr };
        Ling::Button* closeBtn{ nullptr };
        std::wstring text;
        std::vector<WordBlock> words;
        std::atomic<unsigned long long> requestId{ 0 };
        bool busy{ false };
        bool internalTextUpdate{ false };
    };

    inline State state;

    inline bool containsPoint(POINT pos)
    {
        return state.panel && state.win && state.panel->isPosIn(pos);
    }

    inline bool copyTextReliable(const std::wstring& text)
    {
        if (text.empty()) return false;
        HWND owner = state.win ? state.win->hwnd : nullptr;
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

    inline void setTextViewText(const std::wstring& text)
    {
        state.text = text;
        if (!state.textBox) return;
        state.internalTextUpdate = true;
        state.textBox->setText(text);
        state.internalTextUpdate = false;
    }

    inline void layoutPanel()
    {
        if (!state.win || !state.panel || !state.win->cutMask || !state.win->cutMask->hasRect()) return;

        const auto& r = state.win->cutMask->maskRect;
        const float dpi = (std::max)(0.5f, state.win->dpi);
        const float deskW = state.win->w / dpi;
        const float left = r.left / dpi;
        const float top = r.top / dpi;
        const float right = r.right / dpi;
        const float bottom = r.bottom / dpi;

        constexpr float panelW = 330.f;
        const float panelH = (std::max)(1.f, bottom - top);

        float px = right;
        if (px + panelW > deskW) px = left - panelW;
        if (px < 0.f) px = (std::max)(0.f, right - panelW);

        state.panel->setSize(panelW, panelH);
        state.panel->setPosition(Ling::Edge::Left, px);
        state.panel->setPosition(Ling::Edge::Top, top);

        // Same height as the selection at all times. When the selection is short, remove
        // non-essential chrome instead of allowing any child to paint outside the panel.
        const bool veryCompact = panelH < 58.f;
        const bool compact = panelH < 118.f;

        if (state.header) {
            if (veryCompact) state.header->hide();
            else state.header->show();
        }
        if (state.status) {
            if (compact) state.status->hide();
            else state.status->show();
        }

        if (state.textBox) {
            const float pad = veryCompact ? 2.f : 8.f;
            const float headerH = veryCompact ? 0.f : 30.f;
            const float statusH = compact ? 0.f : 22.f;
            const float gap = veryCompact ? 0.f : (compact ? 3.f : 5.f);
            const float textTop = pad + headerH + statusH + gap;
            const float textH = (std::max)(1.f, panelH - textTop - pad);
            state.textBox->setPositionType(Ling::Position::Absolute);
            state.textBox->setPosition(Ling::Edge::Left, pad);
            state.textBox->setPosition(Ling::Edge::Top, textTop);
            state.textBox->setSize((std::max)(1.f, panelW - pad * 2.f), textH);
            state.textBox->show();
        }
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
        bool apartmentReady = false;
        std::wstring paddleFailure;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentReady = true;

            // First choice: bundled, fully local PaddleOCR-json. It provides much better
            // screenshot OCR accuracy and already returns bounding boxes + confidence.
            auto paddle = WeShotPaddleOcr::recognize(pixels, width, height);
            if (paddle.available && paddle.success) {
                result.text = std::move(paddle.text);
                result.words.reserve(paddle.blocks.size());
                for (auto& b : paddle.blocks) {
                    result.words.push_back({ std::move(b.text), b.x, b.y, b.w, b.h });
                }
                result.engine = L"PaddleOCR";
                winrt::uninit_apartment();
                return result;
            }
            if (paddle.available) paddleFailure = paddle.error;

            // Compatibility fallback: Windows OCR. This keeps OCR usable even if the bundled
            // engine cannot start on an unusual CPU or its files were removed.
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
                result.error = paddleFailure.empty()
                    ? L"当前 Windows 没有可用的 OCR 语言包。"
                    : std::wstring(L"PaddleOCR 不可用：") + paddleFailure + L"；Windows OCR 也不可用。";
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
                else result.engine = paddleFailure.empty() ? L"Windows OCR" : L"Windows OCR（Paddle兜底）";
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

    inline void ensurePanel(WinCap* win)
    {
        if (!win) return;
        if (state.win == win && state.panel) {
            state.panel->show();
            if (state.textBox) state.textBox->show();
            layoutPanel();
            return;
        }

        state.win = win;
        state.panel = win->body->makeChild<Ling::Node>();
        state.panel->setPositionType(Ling::Position::Absolute);
        state.panel->setFlexDirection(Ling::FlexDirection::Column);
        state.panel->setBg(0xFFFFFFFF);
        state.panel->setBorder(1.f, 0xD4D4D4FF);
        state.panel->setBorderRadius(0.f);

        state.header = state.panel->makeChild<Ling::Node>();
        state.header->setPositionType(Ling::Position::Absolute);
        state.header->setPosition(Ling::Edge::Left, 8.f);
        state.header->setPosition(Ling::Edge::Top, 4.f);
        state.header->setSize(314.f, 30.f);
        state.header->setFlexDirection(Ling::FlexDirection::Row);
        state.header->setAlignItems(Ling::Align::Center);

        state.title = state.header->makeChild<Ling::Label>();
        state.title->setText(L"文字识别");
        state.title->setFontSize(15.f);
        state.title->setColor(0x222222FF);
        state.title->setFlexGrow(1.f);

        state.copyBtn = state.header->makeChild<Ling::Button>();
        state.copyBtn->setText(L"复制全部");
        state.copyBtn->setSize(68.f, 26.f);
        state.copyBtn->setFontSize(12.f);
        state.copyBtn->setColor(0x333333FF);
        state.copyBtn->setBg(0xF5F5F5FF);
        state.copyBtn->setHoverBg(0xEBEBEBFF);
        state.copyBtn->setBorderRadius(5.f);
        state.copyBtn->setMarginRight(6.f);
        state.copyBtn->onClick.add([](Ling::Button*) {
            const bool ok = copyTextReliable(state.text);
            if (state.status) state.status->setText(ok ? L"已复制全部文字" : L"复制失败，请重试");
        });

        state.closeBtn = state.header->makeChild<Ling::Button>();
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
        state.status->setPositionType(Ling::Position::Absolute);
        state.status->setPosition(Ling::Edge::Left, 8.f);
        state.status->setPosition(Ling::Edge::Top, 34.f);
        state.status->setSize(314.f, 22.f);
        state.status->setText(L"");
        state.status->setFontSize(12.f);
        state.status->setColor(0x888888FF);

        // Composited text view: it stays in the same DirectComposition tree as the screenshot,
        // so it cannot disappear behind the capture surface. TextBox also clips, scrolls and
        // supports mouse selection + Ctrl+C natively.
        state.textBox = state.panel->makeChild<Ling::TextBox>();
        state.textBox->setPositionType(Ling::Position::Absolute);
        state.textBox->setFontSize(14.f);
        state.textBox->setPadding(8.f);
        state.textBox->setBg(0xFAFAFAFF);
        state.textBox->setBorder(1.f, 0xE3E3E3FF);
        state.textBox->setBorderRadius(3.f);
        state.textBox->setSelectionBgColor(0xB8DDF799);
        state.textBox->setCaretColor(0x00000000);
        state.textBox->onTextChanged.add([](Ling::TextBox* box, const std::wstring& value) {
            if (state.internalTextUpdate || value == state.text) return;
            // Keep OCR output read-only while retaining selection/copy behavior.
            state.internalTextUpdate = true;
            box->setText(state.text);
            state.internalTextUpdate = false;
        });

        win->onMouseMove.add([](POINT) { if (state.panel) layoutPanel(); });
        win->onDpiChanged.add([]() { if (state.panel) layoutPanel(); });
        win->onDestroy.add([]() {
            state.requestId.fetch_add(1);
            state.win = nullptr;
            state.panel = nullptr;
            state.header = nullptr;
            state.title = nullptr;
            state.status = nullptr;
            state.textBox = nullptr;
            state.copyBtn = nullptr;
            state.closeBtn = nullptr;
            state.text.clear();
            state.words.clear();
            state.busy = false;
            state.internalTextUpdate = false;
        });

        layoutPanel();
        state.panel->show();
        state.textBox->show();
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
            if (state.status) state.status->setText(L"读取截图失败");
            setTextViewText(L"");
            return;
        }

        state.busy = true;
        if (state.status) state.status->setText(L"正在识别...");
        state.textBox->setPlaceholder(L"正在识别...");
        setTextViewText(L"");
        const auto myRequest = ++state.requestId;

        std::thread([pixels = std::move(pixels), width, height, myRequest, win]() mutable {
            auto result = recognize(std::move(pixels), width, height);
            Ling::App::get()->dq.TryEnqueue([result = std::move(result), myRequest, win]() mutable {
                if (state.win != win || state.requestId.load() != myRequest || !state.textBox) return;
                state.busy = false;
                state.words = std::move(result.words);
                if (!result.error.empty()) {
                    if (state.status) state.status->setText(result.error);
                    state.textBox->setPlaceholder(result.error);
                    setTextViewText(L"");
                }
                else {
                    if (state.status) {
                        auto prefix = result.engine.empty() ? L"OCR" : result.engine;
                        state.status->setText(std::wstring(prefix) + L" · 拖选文字后按 Ctrl+C 可复制");
                    }
                    state.textBox->setPlaceholder(L"");
                    setTextViewText(result.text);
                }
                if (state.panel) state.panel->show();
                if (state.textBox) state.textBox->show();
                layoutPanel();
            });
        }).detach();
    }
}
