#pragma once

#include "ClipboardHistoryV099Enhance.h"
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
// function so we can wrap it with an off-screen buffer before the ListBox starts
// calling v099DrawListItem from the later source parts.
#define v099DrawListItem v099DrawListItemDirect
#include "ClipboardHistoryV099Utools_part5.inc"
#undef v099DrawListItem
#include "ClipboardHistoryV099BufferedRows.inc"

#include "ClipboardHistoryV099Utools_part6.inc"
#include "ClipboardHistoryV099Utools_part7.inc"
#include "ClipboardHistoryV099Utools_part8.inc"
#include "ClipboardHistoryV099Utools_part9.inc"
#include "ClipboardHistoryV099Polish.h"
#include "ClipboardHistoryV099Stable.h"
#include "ClipboardHistoryV099PointerFix.h"
