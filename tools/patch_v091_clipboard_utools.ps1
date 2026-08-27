$ErrorActionPreference = 'Stop'

# Replace the temporary v0.8.15 clipboard header generated earlier in the patch
# chain with the native v0.9.1 implementation. App.cpp / Tray.cpp / Setting.cpp
# keep including ClipboardHistory.h, so the rest of the application stays stable.
Set-Content 'Src\ClipboardHistory.h' @'
#pragma once
#include "ClipboardHistoryV091.h"
'@ -Encoding utf8

$verify = Get-Content 'Src\ClipboardHistoryV091.h' -Raw
foreach ($needle in @(
    'MAX_ITEMS = 800',
    'MAX_AGE_DAYS = 14',
    'Tab::Favorite',
    'EM_SETCUEBANNER',
    'multiHashes',
    'saveStore();',
    'inline void toggle()'
)) {
    if (-not $verify.Contains($needle)) { throw "v0.9.1 clipboard verification failed: $needle" }
}
Write-Host 'v0.9.1 uTools-style native clipboard manager applied.'
