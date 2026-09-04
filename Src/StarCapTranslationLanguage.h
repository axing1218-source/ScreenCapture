#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include "Setting.h"

namespace StarCapTranslationLanguage
{
    struct Option
    {
        const wchar_t* label;
        const wchar_t* prompt;
    };

    inline constexpr std::array<Option, 12> options{{
        { L"简体中文", L"Simplified Chinese" },
        { L"繁體中文", L"Traditional Chinese" },
        { L"English", L"English" },
        { L"Español", L"Spanish" },
        { L"Français", L"French" },
        { L"Deutsch", L"German" },
        { L"Italiano", L"Italian" },
        { L"Português", L"Portuguese" },
        { L"日本語", L"Japanese" },
        { L"한국어", L"Korean" },
        { L"Русский", L"Russian" },
        { L"العربية", L"Arabic" },
    }};

    inline int clampIndex(int index)
    {
        return std::clamp(index, 0, (int)options.size() - 1);
    }

    // Stored through Setting's existing persistent numeric tool settings so old
    // config.json files remain valid without a schema migration. Index 0 is the
    // fresh-install default: Simplified Chinese.
    inline int defaultIndex()
    {
        auto* setting = Setting::get();
        if (!setting) return 0;
        return clampIndex((int)std::lround(
            setting->getToolNum(L"translation", L"targetLanguageIndex", 0.f)));
    }

    inline void setDefaultIndex(int index)
    {
        if (auto* setting = Setting::get())
            setting->setToolNum(L"translation", L"targetLanguageIndex", (float)clampIndex(index));
    }

    inline int sessionIndex{ -1 };

    inline void resetSessionToDefault()
    {
        sessionIndex = defaultIndex();
    }

    inline void clearSession()
    {
        sessionIndex = -1;
    }

    inline int activeIndex()
    {
        return sessionIndex >= 0 ? clampIndex(sessionIndex) : defaultIndex();
    }

    inline std::wstring label(int index)
    {
        return options[clampIndex(index)].label;
    }

    inline std::wstring prompt(int index)
    {
        return options[clampIndex(index)].prompt;
    }

    inline std::wstring activePrompt()
    {
        return prompt(activeIndex());
    }
}
