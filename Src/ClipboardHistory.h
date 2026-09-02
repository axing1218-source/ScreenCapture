#pragma once

// Keep the proven clipboard storage/capture engine intact, but place it in a
// private namespace so v0.9.9 can provide a new responsive presentation layer.
#define ClipboardHistory ClipboardHistoryLegacy
#include "ClipboardHistoryImpl.h"
#undef ClipboardHistory

// Win32 RECT/MONITORINFO coordinates are LONG while the responsive layout uses
// int constants. MSVC intentionally does not choose between std::max<int> and
// std::max<LONG>, so provide the two exact mixed overloads used by this UI.
namespace std
{
    inline int max(int a, LONG b) noexcept { return a > b ? a : static_cast<int>(b); }
    inline int min(int a, LONG b) noexcept { return a < b ? a : static_cast<int>(b); }
}

namespace ClipboardHistory
{
    // Keep the normal six-argument draw helper visible inside the new namespace.
    inline void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color,
        HFONT font, UINT flags)
    {
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }

    // The item-number call site uses the current owner-draw clip as its row
    // rectangle. This small overload supplies the missing rectangle there.
    inline void drawText(HDC dc, const std::wstring& text, COLORREF color, HFONT font, UINT flags)
    {
        RECT clip{};
        GetClipBox(dc, &clip);
        RECT rc{ clip.right - 51, clip.bottom - 29, clip.right - 13, clip.bottom - 3 };
        ClipboardHistoryLegacy::drawText(dc, text, rc, color, font, flags);
    }
}

#include "ClipboardHistoryV099.h"
