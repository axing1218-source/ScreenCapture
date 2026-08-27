$ErrorActionPreference = 'Stop'
$path = 'Src\ClipboardHistory.h'
$src = Get-Content $path -Raw

$old = @'
    inline void layoutHistory(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int pad = 10, btnH = 30, hintH = 24, gap = 8;
        const int bottomY = rc.bottom - pad - btnH;
        if (listWnd) MoveWindow(listWnd, pad, pad + hintH,
            std::max(40, rc.right - pad * 2), std::max(40, bottomY - gap - (pad + hintH)), TRUE);
        auto copy = GetDlgItem(hwnd, ID_COPY), del = GetDlgItem(hwnd, ID_DELETE), clear = GetDlgItem(hwnd, ID_CLEAR);
        if (copy) MoveWindow(copy, pad, bottomY, 110, btnH, TRUE);
        if (del) MoveWindow(del, pad + 118, bottomY, 90, btnH, TRUE);
        if (clear) MoveWindow(clear, pad + 216, bottomY, 90, btnH, TRUE);
        auto hint = GetDlgItem(hwnd, 3199);
        if (hint) MoveWindow(hint, pad, pad, std::max(40, rc.right - pad * 2), hintH, TRUE);
    }
'@
$new = @'
    inline void layoutHistory(HWND hwnd)
    {
        RECT rc{}; GetClientRect(hwnd, &rc);
        const int pad = 10, btnH = 30, hintH = 24, gap = 8;
        const int clientW = (int)(rc.right - rc.left);
        const int clientH = (int)(rc.bottom - rc.top);
        const int bottomY = clientH - pad - btnH;
        const int contentW = std::max(40, clientW - pad * 2);
        const int listH = std::max(40, bottomY - gap - (pad + hintH));
        if (listWnd) MoveWindow(listWnd, pad, pad + hintH, contentW, listH, TRUE);
        auto copy = GetDlgItem(hwnd, ID_COPY), del = GetDlgItem(hwnd, ID_DELETE), clear = GetDlgItem(hwnd, ID_CLEAR);
        if (copy) MoveWindow(copy, pad, bottomY, 110, btnH, TRUE);
        if (del) MoveWindow(del, pad + 118, bottomY, 90, btnH, TRUE);
        if (clear) MoveWindow(clear, pad + 216, bottomY, 90, btnH, TRUE);
        auto hint = GetDlgItem(hwnd, 3199);
        if (hint) MoveWindow(hint, pad, pad, contentW, hintH, TRUE);
    }
'@
if (-not $src.Contains($old)) { throw 'v0.8.15 compile fix target not found: layoutHistory' }
$src = $src.Replace($old, $new)
$src = $src.Replace('(HMENU)3199', 'reinterpret_cast<HMENU>((INT_PTR)3199)')
$src = $src.Replace('(HMENU)ID_LIST', 'reinterpret_cast<HMENU>((INT_PTR)ID_LIST)')
$src = $src.Replace('(HMENU)ID_COPY', 'reinterpret_cast<HMENU>((INT_PTR)ID_COPY)')
$src = $src.Replace('(HMENU)ID_DELETE', 'reinterpret_cast<HMENU>((INT_PTR)ID_DELETE)')
$src = $src.Replace('(HMENU)ID_CLEAR', 'reinterpret_cast<HMENU>((INT_PTR)ID_CLEAR)')
Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
if (-not $verify.Contains('const int contentW = std::max(40, clientW - pad * 2);')) { throw 'v0.8.15 compile fix verification failed' }
Write-Host 'v0.8.15 clipboard layout compile fix applied.'

& (Join-Path $PSScriptRoot 'patch_v016_adaptive_typography.ps1')
& (Join-Path $PSScriptRoot 'patch_v017_coordinate_normalization_fixed.ps1')
& (Join-Path $PSScriptRoot 'patch_v018_geometry_regularization.ps1')
& (Join-Path $PSScriptRoot 'patch_v019_stable_geometry.ps1')
& (Join-Path $PSScriptRoot 'patch_v020_wrapper.ps1')
& (Join-Path $PSScriptRoot 'patch_v021_wrapper.ps1')
& (Join-Path $PSScriptRoot 'patch_v023_multiscale_ocr.ps1')
& (Join-Path $PSScriptRoot 'patch_v022_wrapper.ps1')
& (Join-Path $PSScriptRoot 'patch_v023_wrapper.ps1')

# v0.9.0: central version era + configurable clipboard-window toggle shortcut.
& (Join-Path $PSScriptRoot 'patch_v090_foundation.ps1')
# v0.9.1: high-fidelity uTools-style clipboard manager prototype.
& (Join-Path $PSScriptRoot 'patch_v091_clipboard_utools.ps1')
# v0.9.2: image readability / zoom / compile compatibility.
& (Join-Path $PSScriptRoot 'patch_v092_clipboard_visual.ps1')
& (Join-Path $PSScriptRoot 'patch_v092_compilefix.ps1')
# v0.9.3: current official uTools-style layout and interactions + real preview window + explicit theme switch.
& (Join-Path $PSScriptRoot 'patch_v093_clipboard_official.ps1')
