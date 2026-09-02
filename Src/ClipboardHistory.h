#pragma once

// Keep the proven clipboard storage/capture engine intact, but place it in a
// private namespace so v0.9.9 can provide a new responsive presentation layer.
#define ClipboardHistory ClipboardHistoryLegacy
#include "ClipboardHistoryImpl.h"
#undef ClipboardHistory

#include "ClipboardHistoryV099.h"
