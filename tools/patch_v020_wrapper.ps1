$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot 'patch_v020_local_ocr_geometry.ps1'
$fixedPath = Join-Path $PSScriptRoot '_patch_v020_local_ocr_geometry_fixed.ps1'
$text = Get-Content $sourcePath -Raw

$bad = @'
    $src = $src.Replace('#include "GeminiClient.h"', "#include \"GeminiClient.h\"`r`n#include \"WeShotTextGeometry.h\"")
'@
$good = @'
    $includeOld = '#include "GeminiClient.h"'
    $includeNew = @"
#include "GeminiClient.h"
#include "WeShotTextGeometry.h"
"@
    $src = $src.Replace($includeOld, $includeNew.TrimEnd())
'@

$count = ([regex]::Matches($text, [regex]::Escape($bad.TrimEnd("`r","`n")))).Count
if ($count -ne 2) { throw "Expected 2 v0.8.20 quoting targets, found $count" }
$text = $text.Replace($bad.TrimEnd("`r","`n"), $good.TrimEnd("`r","`n"))
Set-Content $fixedPath $text -Encoding utf8

& $fixedPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
