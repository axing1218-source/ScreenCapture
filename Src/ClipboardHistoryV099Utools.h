#pragma once

#include "Setting.h"
#include "ClipboardHistoryV099Enhance.h"

// part1 and part2 are one physical source unit split in the middle of the
// embedded logo bitmap. Keep them contiguous while renaming the original light
// palette, then expose the dynamic app palette before the remaining UI parts.
#define v099Canvas v099CanvasLight
#define v099Card v099CardLight
#define v099Surface v099SurfaceLight
#define v099Border v099BorderLight
#define v099Text v099TextLight
#define v099Muted v099MutedLight
#define v099Faint v099FaintLight
#define v099Primary v099PrimaryLight
#define v099PrimarySoft v099PrimarySoftLight
#define v099Hover v099HoverLight
#define v099SelectedBg v099SelectedBgLight
#define v099SyncBrushes v099SyncBrushesLight
#define v099ApplyRoundRegion v099ApplyRoundRegionLight
#include "ClipboardHistoryV099Utools_part1.inc"
#include "ClipboardHistoryV099Utools_part2.inc"
#undef v099Canvas
#undef v099Card
#undef v099Surface
#undef v099Border
#undef v099Text
#undef v099Muted
#undef v099Faint
#undef v099Primary
#undef v099PrimarySoft
#undef v099Hover
#undef v099SelectedBg
#undef v099SyncBrushes
#undef v099ApplyRoundRegion

namespace ClipboardHistory
{
    inline bool v099DarkMode()
    {
        auto* s = Setting::get();
        return s && s->getToolFlag(L"app", L"darkMode", false);
    }

    inline COLORREF v099Canvas()      { return v099DarkMode() ? RGB(30, 31, 34) : RGB(254, 254, 254); }
    inline COLORREF v099Card()        { return v099DarkMode() ? RGB(37, 38, 42) : RGB(255, 255, 255); }
    inline COLORREF v099Surface()     { return v099DarkMode() ? RGB(43, 44, 48) : RGB(244, 244, 244); }
    inline COLORREF v099Border()      { return v099DarkMode() ? RGB(62, 64, 69) : RGB(229, 231, 235); }
    inline COLORREF v099Text()        { return v099DarkMode() ? RGB(232, 234, 237) : RGB(32, 33, 36); }
    inline COLORREF v099Muted()       { return v099DarkMode() ? RGB(174, 178, 185) : RGB(112, 117, 124); }
    inline COLORREF v099Faint()       { return v099DarkMode() ? RGB(137, 141, 148) : RGB(154, 160, 166); }
    inline COLORREF v099Primary()     { return RGB(88, 112, 255); }
    inline COLORREF v099PrimarySoft() { return v099DarkMode() ? RGB(47, 52, 76) : RGB(240, 243, 255); }
    inline COLORREF v099Hover()       { return v099DarkMode() ? RGB(42, 43, 47) : RGB(249, 250, 252); }
    inline COLORREF v099SelectedBg()  { return v099DarkMode() ? RGB(35, 37, 44) : RGB(255, 255, 255); }

    inline void v099SyncBrushes()
    {
        if (v099ListBrush) DeleteObject(v099ListBrush);
        if (v099SearchBrush) DeleteObject(v099SearchBrush);
        v099ListBrush = CreateSolidBrush(v099Canvas());
        v099SearchBrush = CreateSolidBrush(v099Surface());
        if (ClipboardHistoryLegacy::editBrush) DeleteObject(ClipboardHistoryLegacy::editBrush);
        ClipboardHistoryLegacy::editBrush = CreateSolidBrush(v099Surface());
    }

    inline void v099ApplyRoundRegion(HWND hwnd)
    {
        ClipboardHistoryWindowShim::applyNativeFrame(hwnd, v099Border());
    }

    inline void v099RefreshTheme()
    {
        ClipboardHistoryLegacy::themeMode = v099DarkMode()
            ? ClipboardHistoryLegacy::ThemeMode::Dark
            : ClipboardHistoryLegacy::ThemeMode::Light;
        v099SyncBrushes();

        if (ClipboardHistoryWindowShim::previewEditBrush) {
            DeleteObject(ClipboardHistoryWindowShim::previewEditBrush);
            ClipboardHistoryWindowShim::previewEditBrush = nullptr;
        }

        if (ClipboardHistoryLegacy::historyWnd && IsWindow(ClipboardHistoryLegacy::historyWnd))
            RedrawWindow(ClipboardHistoryLegacy::historyWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        if (ClipboardHistoryLegacy::listWnd && IsWindow(ClipboardHistoryLegacy::listWnd))
            RedrawWindow(ClipboardHistoryLegacy::listWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        if (ClipboardHistoryLegacy::fullWnd && IsWindow(ClipboardHistoryLegacy::fullWnd))
            RedrawWindow(ClipboardHistoryLegacy::fullWnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

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

#include "ClipboardHistoryV099Polish.h"
#include "ClipboardHistoryV099Stable.h"
#include "ClipboardHistoryV099PointerFix.h"
#include "ClipboardHistoryV099FinalFix.h"
#include "ClipboardHistoryV099FilePreview.h"
