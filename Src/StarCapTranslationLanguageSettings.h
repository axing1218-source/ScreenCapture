#pragma once

#include <include/Ling.h>
#include <cmath>
#include "StarCapTranslationLanguage.h"

namespace StarCapTranslationLanguageSettings
{
    inline bool darkMode()
    {
        auto* setting = Setting::get();
        return setting && setting->getToolFlag(L"app", L"darkMode", false);
    }

    inline uint32_t textColor() { return darkMode() ? 0xE8EAEDFF : 0x333333FF; }
    inline uint32_t surfaceColor() { return darkMode() ? 0x2B2C30FF : 0xFFFFFFFF; }
    inline uint32_t borderColor() { return darkMode() ? 0x4A4C52FF : 0xE0E0E0FF; }
    inline uint32_t hoverColor() { return darkMode() ? 0x383A3FFF : 0xF5F5F5FF; }

    inline void showMenu(Ling::Node* root, Ling::Button* btn)
    {
        if (!root || !btn || !root->win) return;
        HMENU menu = CreatePopupMenu();
        if (!menu) return;

        const int current = StarCapTranslationLanguage::defaultIndex();
        constexpr UINT baseId = 4100;
        for (size_t i = 0; i < StarCapTranslationLanguage::options.size(); ++i) {
            UINT flags = MF_STRING;
            if ((int)i == current) flags |= MF_CHECKED;
            AppendMenuW(menu, flags, baseId + (UINT)i,
                StarCapTranslationLanguage::options[i].label);
        }

        POINT pt{
            (LONG)std::lround(btn->x),
            (LONG)std::lround(btn->y + btn->h)
        };
        ClientToScreen(root->win->hwnd, &pt);
        SetForegroundWindow(root->win->hwnd);
        const UINT cmd = TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            pt.x, pt.y, 0, root->win->hwnd, nullptr);
        DestroyMenu(menu);
        PostMessageW(root->win->hwnd, WM_NULL, 0, 0);

        if (cmd < baseId || cmd >= baseId + StarCapTranslationLanguage::options.size()) return;
        const int selected = (int)(cmd - baseId);
        StarCapTranslationLanguage::setDefaultIndex(selected);
        btn->setText(StarCapTranslationLanguage::label(selected) + L"  ▾");
    }

    inline void attach(Ling::Node* root)
    {
        if (!root) return;

        auto row = root->makeChild<Ling::Node>();
        row->setHeight(39.f);
        row->setFlexDirection(Ling::FlexDirection::Row);
        row->setAlignItems(Ling::Align::Center);

        auto label = row->makeChild<Ling::Label>();
        label->setText(L"默认翻译语言");
        label->setColor(textColor());
        label->setHeightPercent(100.f);
        label->setJustifyContent(Ling::Justify::Center);
        label->setFlexGrow(1.f);

        auto btn = row->makeChild<Ling::Button>();
        btn->setText(StarCapTranslationLanguage::label(
            StarCapTranslationLanguage::defaultIndex()) + L"  ▾");
        btn->setColor(textColor());
        btn->setHoverColor(textColor());
        btn->setBg(darkMode() ? surfaceColor() : 0x00000000);
        btn->setSize(160.f, 28.f);
        btn->setBorder(1.f, borderColor());
        btn->setBorderRadius(4.f);
        btn->setHoverBg(hoverColor());
        btn->onClick.add([root](Ling::Button* clicked) {
            showMenu(root, clicked);
        });

        auto border = root->makeChild<Ling::Node>();
        border->setHeight(1.f);
        border->setBg(borderColor());
    }
}
