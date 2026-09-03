#pragma once

#include "Setting.h"

// ClipboardHistoryV099Enhance predates the app-level theme and contains a small
// fixed light palette. Route only those known UI colors through the saved app
// preference while the header is compiled; logo colors and unrelated RGB values
// pass through unchanged.
namespace ClipboardHistoryV099ThemeBootstrap
{
    inline bool dark()
    {
        auto* setting = Setting::get();
        return setting && setting->getToolFlag(L"app", L"darkMode", false);
    }

    inline COLORREF rgb(int r, int g, int b)
    {
        const COLORREF original = RGB(r, g, b);
        if (!dark()) return original;
        if (r==254 && g==254 && b==254) return RGB(30,31,34);
        if (r==255 && g==255 && b==255) return RGB(37,38,42);
        if (r==244 && g==244 && b==244) return RGB(43,44,48);
        if (r==229 && g==231 && b==235) return RGB(62,64,69);
        if (r==32 && g==33 && b==36) return RGB(232,234,237);
        if (r==112 && g==117 && b==124) return RGB(174,178,185);
        if (r==240 && g==243 && b==255) return RGB(47,52,76);
        if (r==210 && g==219 && b==255) return RGB(73,80,112);
        if (r==150 && g==154 && b==160) return RGB(133,137,145);
        if (r==184 && g==187 && b==191) return RGB(105,109,117);
        return original;
    }
}

#pragma push_macro("RGB")
#undef RGB
#define RGB(r,g,b) ClipboardHistoryV099ThemeBootstrap::rgb((r),(g),(b))
#include "ClipboardHistoryV099Enhance.h"
#pragma pop_macro("RGB")

#include "ClipboardHistoryV099Utools_part1.inc"
#include "ClipboardHistoryV099Utools_part2.inc"

// The v0.9.9 source is split in the middle of several function bodies. Rename
// the legacy bitmap logo at preprocessing time, then route only the later call
// site to the anti-aliased vector implementation without inserting a function
// definition between those split bodies.
#define v099DrawAppLogo v099DrawAppLogoLegacy
#include "ClipboardHistoryV099Utools_part3.inc"
#undef v099DrawAppLogo
#define v099DrawAppLogo v099DrawStarCapLogoAA
#include "ClipboardHistoryV099Utools_part4.inc"
#undef v099DrawAppLogo

// Keep the existing row layout code, but rename the direct-to-screen owner-draw
// implementation. part6 forward-declares the buffered public entry so the later
// WM_DRAWITEM handler can call it safely.
#define v099DrawListItem v099DrawListItemDirect
#include "ClipboardHistoryV099Utools_part5.inc"
#undef v099DrawListItem

#include "ClipboardHistoryV099Utools_part6.inc"
#include "ClipboardHistoryV099Utools_part7.inc"
#include "ClipboardHistoryV099Utools_part8.inc"
#include "ClipboardHistoryV099Utools_part9.inc"

// part9 closes namespace ClipboardHistory, so reopen it here to provide the
// buffered owner-draw implementation declared in part6.
namespace ClipboardHistory
{
#include "ClipboardHistoryV099BufferedRows.inc"
}

#include "ClipboardHistoryV099Theme.h"
#include "ClipboardHistoryV099Polish.h"
#include "ClipboardHistoryV099Stable.h"
#include "ClipboardHistoryV099PointerFix.h"
#include "ClipboardHistoryV099FinalFix.h"
#include "ClipboardHistoryV099FilePreview.h"
#include "ClipboardHistoryV099FilePreviewFix.h"
#include "ClipboardHistoryV099FilePreviewDragFix.h"
#include "ClipboardHistoryV099DarkPreviewFix.h"
#include "ClipboardHistoryV099PreviewFrameFix.h"
#include "ClipboardHistoryV099DirectPreview.h"
#include "ClipboardHistoryV099PreviewBackdrop.h"
