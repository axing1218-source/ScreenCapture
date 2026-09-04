#pragma once

#include <memory>
#include <cmath>
#include "StarCapTranslationLanguage.h"

namespace StarCapOcrTranslationLanguageUI
{
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
                if (owner->originalTab) owner->originalTab->hide();
                if (owner->translatedTab) owner->translatedTab->hide();
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
            if (activeLanguageWindow == window) detach(window);
        });
    }
}
