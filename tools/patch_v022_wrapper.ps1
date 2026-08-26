$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'patch_v022_constraint_layout.ps1'
$fixedPath = Join-Path $PSScriptRoot '_patch_v022_constraint_layout_fixed.ps1'
$text = Get-Content $sourcePath -Raw
$bad = 'throw "v0.8.22 verification failed in $p: $needle"'
$good = 'throw "v0.8.22 verification failed in ${p}: $needle"'
if (-not $text.Contains($bad)) { throw 'v0.8.22 wrapper target not found' }
$text = $text.Replace($bad, $good)
Set-Content $fixedPath $text -Encoding utf8
& $fixedPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
