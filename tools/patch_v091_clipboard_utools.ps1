$ErrorActionPreference = 'Stop'

$implPath = 'Src\ClipboardHistoryV091.h'
$impl = Get-Content $implPath -Raw
$impl = $impl.Replace('#include <windows.h>', "#include <windows.h>`r`n#include <windowsx.h>`r`n#include <cstdlib>")
$impl = $impl.Replace('DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));', 'DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, 32768);')
$impl = $impl.Replace('std::filesystem::path root = (n > 0 && n < std::size(buf))', 'std::filesystem::path root = (n > 0 && n < 32768)')
$impl = $impl.Replace("std::wstring s((size_t)n, L'\0');`r`n        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);`r`n        return s;", "std::wstring s((size_t)n + 1, L'\0');`r`n        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);`r`n        s.resize((size_t)n);`r`n        return s;")
$impl = $impl.Replace("std::wstring s((size_t)n, L'\0');`n        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);`n        return s;", "std::wstring s((size_t)n + 1, L'\0');`n        if (n) GetWindowTextW(searchWnd, s.data(), n + 1);`n        s.resize((size_t)n);`n        return s;")

# RECT uses LONG while the constants below are int. Make the conversions explicit
# so MSVC does not reject std::max template deduction on Win32 geometry values.
$impl = $impl.Replace(
    'int aw = std::max(1, rc.right - rc.left), ah = std::max(1, rc.bottom - rc.top);',
    'int aw = std::max(1, (int)(rc.right - rc.left)), ah = std::max(1, (int)(rc.bottom - rc.top));')
$impl = $impl.Replace(
    'int y = itemRc.top + std::max(4, ((itemRc.bottom - itemRc.top) - bw) / 2);',
    'int y = (int)itemRc.top + std::max(4, ((int)(itemRc.bottom - itemRc.top) - bw) / 2);')
Set-Content $implPath $impl -Encoding utf8

# Replace the temporary v0.8.15 clipboard header generated earlier in the patch
# chain with the native v0.9.1 implementation. App.cpp / Tray.cpp / Setting.cpp
# keep including ClipboardHistory.h, so the rest of the application stays stable.
Set-Content 'Src\ClipboardHistory.h' @'
#pragma once
#include "ClipboardHistoryV091.h"
'@ -Encoding utf8

$verify = Get-Content $implPath -Raw
foreach ($needle in @(
    'MAX_ITEMS = 800',
    'MAX_AGE_DAYS = 14',
    'Tab::Favorite',
    'EM_SETCUEBANNER',
    'multiHashes',
    'saveStore();',
    'inline void toggle()',
    '#include <windowsx.h>',
    's.resize((size_t)n);',
    'std::max(1, (int)(rc.right - rc.left))',
    'std::max(4, ((int)(itemRc.bottom - itemRc.top) - bw) / 2)'
)) {
    if (-not $verify.Contains($needle)) { throw "v0.9.1 clipboard verification failed: $needle" }
}
Write-Host 'v0.9.1 uTools-style native clipboard manager applied.'
