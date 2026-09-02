#pragma once

#include "ClipboardHistoryV099Utools_part1.inc"
#include "ClipboardHistoryV099Utools_part2.inc"
#include "ClipboardHistoryV099Utools_part3.inc"

// The source PNG supplied for the clipboard logo has transparent rounded corners.
// The compact RGB copy used by the Win32 painter cannot carry alpha, so clip it to
// a rounded region before drawing. This restores the original rounded silhouette
// instead of showing the flattened square corners.
namespace ClipboardHistory
{
    inline void v099DrawAppLogoRounded(HDC dc, RECT r)
    {
        fillRoundRect(dc, r, v099Primary(), 8);
        const int saved = SaveDC(dc);
        HRGN clip = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 10, 10);
        if (clip) SelectClipRgn(dc, clip);
        v099DrawAppLogo(dc, r);
        if (saved) RestoreDC(dc, saved);
        if (clip) DeleteObject(clip);
    }
}

// Only the header renderer needs these substitutions. Keep the rest of the UI's
// font ownership/disposal untouched.
#define v099DrawAppLogo v099DrawAppLogoRounded
#define v099TitleFont uiFont
#include "ClipboardHistoryV099Utools_part4.inc"
#undef v099TitleFont
#undef v099DrawAppLogo

#include "ClipboardHistoryV099Utools_part5.inc"
#include "ClipboardHistoryV099Utools_part6.inc"
#include "ClipboardHistoryV099Utools_part7.inc"
#include "ClipboardHistoryV099Utools_part8.inc"
#include "ClipboardHistoryV099Utools_part9.inc"
