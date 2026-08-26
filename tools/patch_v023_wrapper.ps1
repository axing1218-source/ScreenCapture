$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'patch_v023_source_paragraph_layout.ps1'
$fixedPath = Join-Path $PSScriptRoot '_patch_v023_source_paragraph_layout_fixed.ps1'
$text = Get-Content $sourcePath -Raw

$bad = @'
        $src = $src.Replace('#include "WeShotTextGeometry.h"', "#include \"WeShotTextGeometry.h\"`r`n#include \"WeShotParagraphLayout.h\"")
'@
$good = @'
        $includeOld = '#include "WeShotTextGeometry.h"'
        $includeNew = @"
#include "WeShotTextGeometry.h"
#include "WeShotParagraphLayout.h"
"@
        $src = $src.Replace($includeOld, $includeNew.TrimEnd())
'@

$needle = $bad.TrimEnd("`r","`n")
if (-not $text.Contains($needle)) { throw 'v0.8.23 quoting target not found' }
$text = $text.Replace($needle, $good.TrimEnd("`r","`n"))
Set-Content $fixedPath $text -Encoding utf8
& $fixedPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
