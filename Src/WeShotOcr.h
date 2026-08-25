#pragma once
#include "WeShotOcrV2.h"

namespace WeShotOcr
{
    inline bool containsPoint(POINT pos)
    {
        return WeShotOcrV2::containsPoint(pos);
    }

    inline bool hasWindow()
    {
        return WeShotOcrV2::hasWindow();
    }

    inline void show(WinCap* win)
    {
        // Close an older result window before the new request receives its request id.
        // Otherwise the old window's onDestroy invalidates the freshly-created OCR request.
        if (WeShotOcrV2::activeWindow) WeShotOcrV2::activeWindow->close();
        WeShotOcrV2::show(win);
    }
}
