#pragma once

// StarCap extends the header-only OCR window only for target-language UI.  Keep all
// dependencies outside the temporary access-control remap so unrelated headers are
// never affected by it.
#include <robuffer.h>
#include <dwmapi.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include "Win/WinCap.h"
#include "Win/CutMask.h"
#include "Win/WinPin.h"
#include "Util.h"
#include "Setting.h"
#include "GeminiClient.h"
#include "StarCapTranslationLanguage.h"
#include "StarCapTextGeometry.h"
#include "StarCapParagraphLayout.h"

namespace GeminiClient
{
    inline TranslationResult translateImageStarCapTarget(
        const std::vector<BYTE>& pixels, int width, int height,
        const std::wstring& apiKey, const std::wstring& model)
    {
        return translateImage(pixels, width, height, apiKey, model,
            StarCapTranslationLanguage::activePrompt());
    }

    inline TranslationResult translateOcrBlocksStarCapTarget(
        const std::vector<OcrBlock>& sourceBlocks,
        const std::wstring& apiKey, const std::wstring& model)
    {
        return translateOcrBlocks(sourceBlocks, apiKey, model,
            StarCapTranslationLanguage::activePrompt());
    }
}

// Translation keeps V2's established rendering/layout path; only its target language
// is supplied by StarCap's per-window selector (falling back to the persistent default).
// No OCR geometry substitution is performed here: image-side text dragging/linking was
// intentionally removed after dense-text testing showed it was not reliable enough.
#define translateImage translateImageStarCapTarget
#define translateOcrBlocks translateOcrBlocksStarCapTarget
#define private public
#include "StarCapOcrV2.h"
#undef private
#undef translateOcrBlocks
#undef translateImage

#include "StarCapOcrTranslationLanguageUI.h"

namespace StarCapOcr
{
    inline bool containsPoint(POINT pos)
    {
        return StarCapOcrV2::containsPoint(pos);
    }

    inline bool hasWindow()
    {
        return StarCapOcrV2::hasWindow();
    }

    inline void showPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapTranslationLanguage::resetSessionToDefault();
        StarCapOcrV2::showPixels(std::move(pixels), width, height, fromLongScreenshot);
        StarCapOcrTranslationLanguageUI::attach(StarCapOcrV2::activeWindow);
    }

    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapTranslationLanguage::resetSessionToDefault();
        StarCapOcrV2::showTranslationPixels(std::move(pixels), width, height, fromLongScreenshot);
        StarCapOcrTranslationLanguageUI::attach(StarCapOcrV2::activeWindow);
    }

    inline void show(WinCap* win)
    {
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapTranslationLanguage::resetSessionToDefault();
        StarCapOcrV2::show(win);
        StarCapOcrTranslationLanguageUI::attach(StarCapOcrV2::activeWindow);
    }
}
