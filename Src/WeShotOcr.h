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
        Ling::Button* copyBtn{ nullptr };
        Ling::Button* closeBtn{ nullptr };
        HWND edit{ nullptr };
        std::wstring text;
        std::vector<WordBlock> words;
        std::atomic<unsigned long long> requestId{ 0 };
        bool busy{ false };
    };

    inline State state;

    inline bool containsPoint(POINT pos)
    {
        if (!state.panel || !state.win) return false;
        return state.panel->isPosIn(pos);
    }

    inline bool copyTextReliable(const std::wstring& text)
    {
        if (text.empty()) return false;
        HWND owner = state.win ? state.win->hwnd : nullptr;
        bool opened = false;
        for (int i = 0; i < 8; ++i) {
            if (OpenClipboard(owner)) {
                opened = true;
                break;
            }
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
                        mem = nullptr; // clipboard owns it now
                    }
                }
                if (mem) GlobalFree(mem);
            }
        }
        CloseClipboard();
        return ok;
    }

    inline void setEditText(const std::wstring& text)
    {
        state.text = text;
        if (!state.edit || !IsWindow(state.edit)) return;
        SetWindowTextW(state.edit, text.c_str());
        SendMessageW(state.edit, EM_SETSEL, 0, 0);
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
        if (px < 0.f) {
            px = (std::max)(0.f, right - panelW);
        }

        state.panel->setSize(panelW, panelH);
        state.panel->setPosition(Ling::Edge::Left, px);
        state.panel->setPosition(Ling::Edge::Top, top);

        // The recognized text is a native read-only EDIT control. Unlike the old composited
        // TextBox it is clipped by its own HWND, so a very short capture can never paint text
        // beyond the OCR panel. It also gives standard Windows selection / Ctrl+C behavior.
        if (state.edit && IsWindow(state.edit)) {
            constexpr float pad = 8.f;
            constexpr float headerH = 30.f;
            const bool compact = panelH < 130.f;
            const float statusH = compact ? 0.f : 22.f;
            const float gap = compact ? 4.f : 6.f;

            if (state.status) {
                if (compact) state.status->hide();
                else state.status->show();
            }

            const float editTop = top + pad + headerH + statusH + gap;
            const float editH = (std::max)(1.f, panelH - (editTop - top) - pad);
            const int ex = (int)std::round((px + pad) * dpi);
            const int ey = (int)std::round(editTop * dpi);
            const int ew = (std::max)(1, (int)std::round((panelW - pad * 2.f) * dpi));
            const int eh = (std::max)(1, (int)std::round(editH * dpi));

            SetWindowPos(state.edit, HWND_TOP, ex, ey, ew, eh,
                SWP_NOACTIVATE | ((eh >= 8) ? SWP_SHOWWINDOW : 0));
            if (eh < 8) ShowWindow(state.edit, SW_HIDE);
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
            if (state.edit && IsWindow(state.edit)) ShowWindow(state.edit, SW_SHOWNA);
            layoutPanel();
            return;
        }

        state.win = win;
        state.panel = win->body->makeChild<Ling::Node>();
        state.panel->setPositionType(Ling::Position::Absolute);
        state.panel->setFlexDirection(Ling::FlexDirection::Column);
        state.panel->setPadding(8.f);
        state.panel->setBg(0xFFFFFFFF);
        state.panel->setBorder(1.f, 0xD4D4D4FF);
        state.panel->setBorderRadius(0.f);

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
            const bool ok = copyTextReliable(state.text);
            if (state.status) state.status->setText(ok ? L"已复制全部文字" : L"复制失败，请重试");
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
            if (state.edit && IsWindow(state.edit)) ShowWindow(state.edit, SW_HIDE);
        });

        state.status = state.panel->makeChild<Ling::Label>();
        state.status->setText(L"");
        state.status->setHeight(22.f);
        state.status->setFlexShrink(0.f);
        state.status->setFontSize(12.f);
        state.status->setColor(0x888888FF);

        // Native read-only multi-line edit: standard Windows mouse selection, scroll wheel,
        // Ctrl+A / Ctrl+C and strict clipping inside its own rectangle.
        state.edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | ES_NOHIDESEL,
            0, 0, 10, 10,
            win->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (state.edit) {
            SendMessageW(state.edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(state.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        }

        win->onMouseMove.add([](POINT) {
            if (state.panel) layoutPanel();
        });
        win->onDpiChanged.add([]() {
            if (state.panel) layoutPanel();
        });
        win->onDestroy.add([]() {
            state.requestId.fetch_add(1);
            if (state.edit && IsWindow(state.edit)) DestroyWindow(state.edit);
            state.win = nullptr;
            state.panel = nullptr;
            state.title = nullptr;
            state.status = nullptr;
            state.copyBtn = nullptr;
            state.closeBtn = nullptr;
            state.edit = nullptr;
            state.text.clear();
            state.words.clear();
            state.busy = false;
        });

        layoutPanel();
        state.panel->show();
        if (state.edit && IsWindow(state.edit)) ShowWindow(state.edit, SW_SHOWNA);
        win->refresh();
    }

    inline void show(WinCap* win)
    {
        if (!win || !win->cutMask || !win->cutMask->hasRect()) return;
        ensurePanel(win);
        if (!state.edit || state.busy) return;

        std::vector<BYTE> pixels;
        int width{ 0 }, height{ 0 };
        if (!copyCutPixels(win, pixels, width, height)) {
            if (state.status) state.status->setText(L"读取截图失败");
            setEditText(L"");
            return;
        }

        state.busy = true;
        if (state.status) state.status->setText(L"正在识别...");
        setEditText(L"");
        const auto myRequest = ++state.requestId;

        std::thread([pixels = std::move(pixels), width, height, myRequest, win]() mutable {
            auto result = recognize(std::move(pixels), width, height);
            Ling::App::get()->dq.TryEnqueue([result = std::move(result), myRequest, win]() mutable {
                if (state.win != win || state.requestId.load() != myRequest || !state.edit) return;
                state.busy = false;
                state.words = std::move(result.words);
                if (!result.error.empty()) {
                    if (state.status) state.status->setText(result.error);
                    setEditText(L"");
                }
                else {
                    if (state.status) state.status->setText(L"可拖选任意文字，按 Ctrl+C 复制");
                    setEditText(result.text);
                }
                if (state.panel) state.panel->show();
                if (state.edit && IsWindow(state.edit)) ShowWindow(state.edit, SW_SHOWNA);
                layoutPanel();
            });
        }).detach();
    }
}
