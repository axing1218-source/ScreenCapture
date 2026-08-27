$ErrorActionPreference = 'Stop'
$path = 'Src\ClipboardHistoryV091.h'
$src = Get-Content $path -Raw

# v0.9.3's persisted theme helper is intentionally placed beside theme(), before
# the existing storagePath() definition.  Give the compiler a forward declaration
# rather than duplicating the storage-path implementation.
if (-not $src.Contains('inline std::filesystem::path storagePath();')) {
    $needle = '    inline std::filesystem::path themeModePath()'
    if (-not $src.Contains($needle)) { throw 'v0.9.3 compile fix: themeModePath missing' }
    $src = $src.Replace($needle, "    inline std::filesystem::path storagePath();`r`n`r`n$needle")
}

Set-Content $path $src -Encoding utf8
$verify = Get-Content $path -Raw
if (-not $verify.Contains('inline std::filesystem::path storagePath();')) { throw 'v0.9.3 compile fix verification failed' }
Write-Host 'v0.9.3 clipboard compile compatibility fix applied.'
