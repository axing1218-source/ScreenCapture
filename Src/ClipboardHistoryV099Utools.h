#pragma once

#include "ClipboardHistoryV099Enhance.h"
#include "ClipboardHistoryV099Utools_part1.inc"
#include "ClipboardHistoryV099Utools_part2.inc"

// Keep the old embedded 32 px logo data for compatibility, but route the active
// v0.9.9 header through the vector source so its edges are compositor-smooth.
#define v099DrawAppLogo v099DrawAppLogoLegacy
#include "ClipboardHistoryV099Utools_part3.inc"
#undef v099DrawAppLogo

inline void v099DrawAppLogo(HDC dc, RECT r)
{
    v099DrawStarCapLogoAA(dc, r);
}

#include "ClipboardHistoryV099Utools_part4.inc"
#include "ClipboardHistoryV099Utools_part5.inc"
#include "ClipboardHistoryV099Utools_part6.inc"
#include "ClipboardHistoryV099Utools_part7.inc"
#include "ClipboardHistoryV099Utools_part8.inc"
#include "ClipboardHistoryV099Utools_part9.inc"
