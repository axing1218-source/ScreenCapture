$ErrorActionPreference = 'Stop'
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
$src = $src.Replace(' zoomLabel->setTextAlign(DWRITE_TEXT_ALIGNMENT_CENTER);', '')
$src = $src.Replace(' zoomHint->setTextAlign(DWRITE_TEXT_ALIGNMENT_TRAILING);', '')
Set-Content $path $src -Encoding utf8
$verify = Get-Content $path -Raw
if ($verify.Contains('setTextAlign(')) { throw 'Unsupported Label::setTextAlign remains after compile fix.' }
Write-Host 'v0.8.10 Ling Label compile fix applied.'

# v0.8.13: unify normal screenshot translation layout and requested toolbar order.
# It is safe to patch the renderer here; v0.8.12 later adds role/sourceLines metadata
# before the C++ project is compiled.
& (Join-Path $PSScriptRoot 'patch_v013_unified_layout_toolbar_order.ps1')
