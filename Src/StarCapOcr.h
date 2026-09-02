#pragma once
#include "StarCapOcrV2.h"

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
        // Keep the same repeated-launch protection used by normal screenshot OCR.
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapOcrV2::showPixels(std::move(pixels), width, height, fromLongScreenshot);
    }

    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapOcrV2::showTranslationPixels(std::move(pixels), width, height, fromLongScreenshot);
    }

    inline void show(WinCap* win)
    {
        // Close an older result window before the new request receives its request id.
        // Otherwise the old window's onDestroy invalidates the freshly-created OCR request.
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapOcrV2::show(win);
    }
}



