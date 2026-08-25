$ErrorActionPreference = 'Stop'
$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw
$src = $src.Replace(' zoomLabel->setTextAlign(DWRITE_TEXT_ALIGNMENT_CENTER);', '')
$src = $src.Replace(' zoomHint->setTextAlign(DWRITE_TEXT_ALIGNMENT_TRAILING);', '')
Set-Content $path $src -Encoding utf8
$verify = Get-Content $path -Raw
if ($verify.Contains('setTextAlign(')) { throw 'Unsupported Label::setTextAlign remains after compile fix.' }
Write-Host 'v0.8.10 Ling Label compile fix applied.'
