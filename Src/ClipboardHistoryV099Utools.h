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

#include "ClipboardHistoryV099Utools_part5.inc"
#include "ClipboardHistoryV099Utools_part6.inc"
#include "ClipboardHistoryV099Utools_part7.inc"
#include "ClipboardHistoryV099Utools_part8.inc"
#include "ClipboardHistoryV099Utools_part9.inc"
