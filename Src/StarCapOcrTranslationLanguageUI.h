#pragma once

#include <memory>
#include <cmath>
#include "StarCapTranslationLanguage.h"

namespace StarCapOcrTranslationLanguageUI
{
    // The source image and the translated image serve different purposes:
    //   - source image: precise Windows-OCR geometry and linked selection
    //   - translated image: readable visual translation
    // Do not make translated paragraph rectangles pretend to be word geometry.  A
    // dedicated composition child sits above the source canvas while translation is
    // visible, paints a clean #FEFEFE surface and the translated blocks, and temporarily
    // suspends image<->text linked selection. Switching back to the source restores it.
    class TranslationSurfaceOverlay final : public Ling::Canvas
    {
    public:
        explicit TranslationSurfaceOverlay(Ling::WinBase* win) : Ling::Canvas(win) {}

        void setOwner(StarCapOcrV2::OcrResultWindow* value) { owner = value; }

    protected:
        void layout() override
        {
            Ling::Node::layout();
            repaint();
        }

    private:
        void suspendLinkedSelection(bool suspend)
        {
            if (!owner) return;
            if (suspend) {
                if (StarCapOcrLinkedSelection::activeBridgeWindow == owner) {
                    StarCapOcrLinkedSelection::activeBridge = nullptr;
                    StarCapOcrLinkedSelection::activeBridgeWindow = nullptr;
                }
            }
            else if (!StarCapOcrLinkedSelection::activeBridge &&
                StarCapOcrLinkedSelection::bridgeOwner &&
                StarCapOcrV2::activeWindow == owner) {
                StarCapOcrLinkedSelection::activeBridgeWindow = owner;
                StarCapOcrLinkedSelection::activeBridge =
                    StarCapOcrLinkedSelection::bridgeOwner.get();
            }
        }

        void repaint()
        {
            auto* ctx = startPaint();
            if (!ctx) return;
            ctx->Clear(D2D1::ColorF(0.f, 0.f));

            const bool translated = owner && owner->translationReady &&
                owner->showTranslatedImage && !owner->translating &&
                owner->imageW > 0 && owner->imageH > 0;
            suspendLinkedSelection(translated);

            if (translated) {
                const float scale = owner->getImageScale();
                const float dw = owner->imageW * scale;
                const float dh = owner->imageH * scale;
                const float left = (w - dw) * .5f + owner->imageOffsetX;
                const float top = (h - dh) * .5f + owner->imageOffsetY;
                const auto dest = D2D1::RectF(left, top, left + dw, top + dh);

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
                constexpr float c = 254.f / 255.f;
                ctx->CreateSolidColorBrush(D2D1::ColorF(c, c, c, 1.f), bg.GetAddressOf());
                if (bg) ctx->FillRectangle(dest, bg.Get());

                // Reuse the established StarCap paragraph renderer, but on a fully clean
                // surface. Source glyphs can no longer leak between/around Chinese blocks.
                owner->paintTranslationBlocks(ctx, dest);
            }
            finishPaint();
        }

    private:
        StarCapOcrV2::OcrResultWindow* owner{ nullptr };
    };

    class Controller final
    {
    public:
        explicit Controller(StarCapOcrV2::OcrResultWindow* owner) : owner(owner)
        {
            install();
        }

    private:
        void install()
        {
            if (!owner || !owner->translateBtn || !owner->translateBtn->parent) return;
            auto* row = owner->translateBtn->parent;

            // The single Translate/Source button already toggles both states.  Keep the
            // toolbar compact and remove the redundant source/translation tabs.
            if (owner->originalTab) {
                owner->originalTab->hide();
                owner->originalTab = nullptr;
            }
            if (owner->translatedTab) {
                owner->translatedTab->hide();
                owner->translatedTab = nullptr;
            }

            auto* label = row->makeChild<Ling::Label>();
            label->setText(L"翻译为");
            label->setColor(StarCapOcrV2::uiSecondary());
            label->setSize(48.f, 28.f);
            label->setAlignItems(Ling::Align::Center);
            label->setJustifyContent(Ling::Justify::Center);
            label->setPositionType(Ling::Position::Absolute);
            label->setPosition(Ling::Edge::Right, 204.f);
            label->setPosition(Ling::Edge::Top, 4.f);

            targetBtn = row->makeChild<Ling::Button>();
            targetBtn->setText(StarCapTranslationLanguage::label(
                StarCapTranslationLanguage::activeIndex()) + L"  ▾");
            targetBtn->setColor(StarCapOcrV2::uiText());
            targetBtn->setHoverColor(StarCapOcrV2::uiText());
            targetBtn->setBg(StarCapOcrV2::uiSurface());
            targetBtn->setHoverBg(StarCapOcrV2::uiSurfaceHover());
            targetBtn->setBorder(1.f, StarCapOcrV2::uiBorder());
            targetBtn->setBorderRadius(4.f);
            targetBtn->setSize(110.f, 28.f);
            targetBtn->setFontSize(12.f);
            targetBtn->setPositionType(Ling::Position::Absolute);
            targetBtn->setPosition(Ling::Edge::Right, 86.f);
            targetBtn->setPosition(Ling::Edge::Top, 4.f);
            targetBtn->onClick.add([this](Ling::Button*) { showMenu(); });

            if (owner->imageCanvas) {
                overlay = owner->imageCanvas->makeChild<TranslationSurfaceOverlay>();
                overlay->setOwner(owner);
                overlay->setPositionType(Ling::Position::Absolute);
                overlay->setPosition(Ling::Edge::Left, 0.f);
                overlay->setPosition(Ling::Edge::Top, 0.f);
                overlay->setPosition(Ling::Edge::Right, 0.f);
                overlay->setPosition(Ling::Edge::Bottom, 0.f);
            }
            owner->refresh();
        }

        void showMenu()
        {
            if (!owner || !targetBtn) return;
            if (owner->translating) {
                if (owner->status) owner->status->setText(L"翻译进行中，请完成后再切换目标语言");
                return;
            }

            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            constexpr UINT baseId = 5200;
            const int current = StarCapTranslationLanguage::activeIndex();
            for (size_t i = 0; i < StarCapTranslationLanguage::options.size(); ++i) {
                UINT flags = MF_STRING;
                if ((int)i == current) flags |= MF_CHECKED;
                AppendMenuW(menu, flags, baseId + (UINT)i,
                    StarCapTranslationLanguage::options[i].label);
            }

            POINT pt{
                (LONG)std::lround(targetBtn->x),
                (LONG)std::lround(targetBtn->y + targetBtn->h)
            };
            ClientToScreen(owner->hwnd, &pt);
            SetForegroundWindow(owner->hwnd);
            const UINT cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                pt.x, pt.y, 0, owner->hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(owner->hwnd, WM_NULL, 0, 0);

            if (cmd < baseId || cmd >= baseId + StarCapTranslationLanguage::options.size()) return;
            const int selected = (int)(cmd - baseId);
            if (selected == current) return;

            StarCapTranslationLanguage::sessionIndex = StarCapTranslationLanguage::clampIndex(selected);
            targetBtn->setText(StarCapTranslationLanguage::label(selected) + L"  ▾");

            // A target-language change is temporary for this OCR window only. If a previous
            // translation exists, return to the authoritative OCR source and require one new
            // translation request; never translate a translation into the next language.
            if (owner->translationReady) {
                owner->translationReady = false;
                owner->showTranslatedImage = false;
                owner->showingTranslationText = false;
                owner->translatedText.clear();
                owner->translationBlocks.clear();
                if (owner->textBox) {
                    owner->textBox->setBg(StarCapOcrV2::uiTextBoxBg());
                    owner->textBox->setText(owner->originalText);
                }
                if (owner->translateBtn) owner->translateBtn->setText(L"翻译");
            }

            if (owner->status) {
                owner->status->setText(L"目标语言已切换为 " +
                    StarCapTranslationLanguage::label(selected) + L"；点击“翻译”开始");
            }
            owner->refresh();
        }

    private:
        StarCapOcrV2::OcrResultWindow* owner{ nullptr };
        Ling::Button* targetBtn{ nullptr };
        TranslationSurfaceOverlay* overlay{ nullptr };
    };

    inline std::unique_ptr<Controller> controllerOwner;
    inline StarCapOcrV2::OcrResultWindow* activeLanguageWindow{ nullptr };

    inline void detach(StarCapOcrV2::OcrResultWindow* window = nullptr)
    {
        if (window && activeLanguageWindow != window) return;
        controllerOwner.reset();
        activeLanguageWindow = nullptr;
        StarCapTranslationLanguage::clearSession();
    }

    inline void attach(StarCapOcrV2::OcrResultWindow* window)
    {
        detach();
        if (!window) return;
        StarCapTranslationLanguage::resetSessionToDefault();
        controllerOwner = std::make_unique<Controller>(window);
        activeLanguageWindow = window;
        window->onDestroy.add([window]() {
            // If the window is closed while the translated overlay has suspended linked
            // selection, release the bridge explicitly so it never retains the dying owner.
            if (!StarCapOcrLinkedSelection::activeBridgeWindow &&
                StarCapOcrLinkedSelection::bridgeOwner) {
                StarCapOcrLinkedSelection::detach();
            }
            if (activeLanguageWindow == window) detach(window);
        });
    }
}
