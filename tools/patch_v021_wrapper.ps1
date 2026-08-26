$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'patch_v021_visual_geometry.ps1'
$fixedPath = Join-Path $PSScriptRoot '_patch_v021_visual_geometry_fixed.ps1'
$text = Get-Content $sourcePath -Raw
$old = "@('collectVisual', 'visual-edge', 'horizontal-edge', 'sourceName', 'padY = medianH * .32f')"
$new = "@('collectVisual', 'visual-edge', 'Horizontal colour differences', 'sourceName', 'padY = medianH * .32f')"
if (-not $text.Contains($old)) { throw 'v0.8.21 verification token target not found' }
$text = $text.Replace($old, $new)
Set-Content $fixedPath $text -Encoding utf8
& $fixedPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
