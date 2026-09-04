#pragma once

// Keep every dependency outside the access-control/type remapping below.  This lets
// StarCap extend the OCR result window without changing Ling itself or touching the
// large existing OCR implementation while we validate the interaction in a test build.
#include "StarCapOcrLinkedControls.h"
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
#include "GeminiClientHybridGeometry.h"
#include "StarCapTextGeometry.h"
#include "StarCapParagraphLayout.h"

// V2 is header-only. For this feature branch, substitute two small StarCap wrappers
// for its Canvas/TextBox members and expose only V2's own private implementation to the
// bridge. All dependencies above were included before the macros, so their definitions
// and ABI are untouched.
//
// The OCR entry point is also substituted with a hybrid implementation:
//   Gemini       -> authoritative recognized text
//   Windows OCR  -> real word BoundingRect geometry only
// This avoids relying on Gemini box_2d for precise text selection.
#define recognizeImage recognizeImageHybrid
#define TextBox StarCapOcrLinkedTextBox
#define Canvas StarCapOcrLinkedCanvas
#define private public
#include "StarCapOcrV2.h"
#undef private
#undef Canvas
#undef TextBox
#undef recognizeImage

#include "StarCapOcrLinkedSelection.h"

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
        StarCapOcrLinkedSelection::attach(StarCapOcrV2::activeWindow);
    }

    inline void showTranslationPixels(std::vector<BYTE> pixels, int width, int height, bool fromLongScreenshot = false)
    {
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapOcrV2::showTranslationPixels(std::move(pixels), width, height, fromLongScreenshot);
        StarCapOcrLinkedSelection::attach(StarCapOcrV2::activeWindow);
    }

    inline void show(WinCap* win)
    {
        // Close an older result window before the new request receives its request id.
        // Otherwise the old window's onDestroy invalidates the freshly-created OCR request.
        if (StarCapOcrV2::activeWindow) StarCapOcrV2::activeWindow->close();
        StarCapOcrV2::show(win);
        StarCapOcrLinkedSelection::attach(StarCapOcrV2::activeWindow);
    }
}
