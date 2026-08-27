$ErrorActionPreference = 'Stop'
$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw
if (-not $src.Contains('#include <cmath>')) {
    $src = $src.Replace('#include <chrono>', "#include <chrono>`r`n#include <cmath>")
}
Set-Content $path $src -Encoding utf8
$verify = Get-Content $path -Raw
if (-not $verify.Contains('#include <cmath>')) { throw 'v0.9.2 cmath compile fix failed' }
Write-Host 'v0.9.2 clipboard compile compatibility fix applied.'
