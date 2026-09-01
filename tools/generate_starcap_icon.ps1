param(
    [string]$OutputPath = "Src\Res\logo.ico"
)

$ErrorActionPreference = 'Stop'
try { Add-Type -AssemblyName System.Drawing.Common -ErrorAction Stop }
catch { Add-Type -AssemblyName System.Drawing -ErrorAction Stop }

$referenceSize = 1093.0
$referenceCenter = $referenceSize / 2.0
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)

# Small Windows notification-area icons need a slightly larger optical footprint.
# 32px and above intentionally stay at the approved full-size geometry.
$smallIconZoom = @{
    16 = 1.12
    20 = 1.10
    24 = 1.08
}

$shapes = @(
    @{ Color = [Drawing.Color]::FromArgb(255,0,129,253);  Points = @(@(547,100), @(438,418), @(541,575), @(653,421)) },
    @{ Color = [Drawing.Color]::FromArgb(255,249,49,50);  Points = @(@(76,425), @(353,632), @(515,589), @(408,426)) },
    @{ Color = [Drawing.Color]::FromArgb(255,24,180,79);  Points = @(@(1017,425), @(684,426), @(577,589), @(739,632)) },
    @{ Color = [Drawing.Color]::FromArgb(255,254,114,1);  Points = @(@(528,617), @(359,662), @(252,991), @(530,786)) },
    @{ Color = [Drawing.Color]::FromArgb(255,254,189,2);  Points = @(@(564,616), @(558,784), @(837,992), @(732,661)) }
)

function New-StarCapBitmap([int]$Size) {
    $bmp = [Drawing.Bitmap]::new($Size, $Size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    try {
        $g.Clear([Drawing.Color]::Transparent)
        $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy

        $scale = [double]$Size / $referenceSize
        $zoom = if ($smallIconZoom.ContainsKey($Size)) { [double]$smallIconZoom[$Size] } else { 1.0 }

        foreach ($shape in $shapes) {
            $pts = [Drawing.PointF[]]::new($shape.Points.Count)
            for ($i = 0; $i -lt $shape.Points.Count; $i++) {
                $x = $referenceCenter + (($shape.Points[$i][0] - $referenceCenter) * $zoom)
                $y = $referenceCenter + (($shape.Points[$i][1] - $referenceCenter) * $zoom)
                $pts[$i] = [Drawing.PointF]::new([float]($x * $scale), [float]($y * $scale))
            }
            $brush = [Drawing.SolidBrush]::new($shape.Color)
            try { $g.FillPolygon($brush, $pts) } finally { $brush.Dispose() }
        }
    }
    finally { $g.Dispose() }
    return $bmp
}

function Convert-BitmapToIconDib([Drawing.Bitmap]$Bitmap) {
    $w = $Bitmap.Width
    $h = $Bitmap.Height
    $andStride = [int]([math]::Floor(($w + 31) / 32) * 4)
    $ms = [IO.MemoryStream]::new()
    $bw = [IO.BinaryWriter]::new($ms)
    try {
        $bw.Write([uint32]40)
        $bw.Write([int32]$w)
        $bw.Write([int32]($h * 2))
        $bw.Write([uint16]1)
        $bw.Write([uint16]32)
        $bw.Write([uint32]0)
        $bw.Write([uint32]($w * $h * 4))
        $bw.Write([int32]0); $bw.Write([int32]0)
        $bw.Write([uint32]0); $bw.Write([uint32]0)

        for ($y = $h - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $w; $x++) {
                $p = $Bitmap.GetPixel($x, $y)
                $bw.Write([byte]$p.B); $bw.Write([byte]$p.G); $bw.Write([byte]$p.R); $bw.Write([byte]$p.A)
            }
        }

        for ($y = $h - 1; $y -ge 0; $y--) {
            $row = New-Object byte[] $andStride
            for ($x = 0; $x -lt $w; $x++) {
                if ($Bitmap.GetPixel($x, $y).A -eq 0) {
                    $idx = [int][math]::Floor($x / 8)
                    $bit = 7 - ($x % 8)
                    $row[$idx] = $row[$idx] -bor [byte](1 -shl $bit)
                }
            }
            $bw.Write([byte[]]$row)
        }
        $bw.Flush()
        return ,$ms.ToArray()
    }
    finally { $bw.Dispose(); $ms.Dispose() }
}

$frames = @()
foreach ($size in $sizes) {
    $bmp = New-StarCapBitmap $size
    try { $frames += @{ Size = $size; Bytes = (Convert-BitmapToIconDib $bmp) } }
    finally { $bmp.Dispose() }
}

$out = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($out)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
    $offset = 6 + (16 * $frames.Count)
    foreach ($frame in $frames) {
        $d = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
        $writer.Write([byte]$d); $writer.Write([byte]$d); $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frame.Bytes.Length); $writer.Write([uint32]$offset)
        $offset += $frame.Bytes.Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
    $writer.Flush()
    $icoBytes = $out.ToArray()
}
finally { $writer.Dispose(); $out.Dispose() }

$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Force $parent | Out-Null }
[IO.File]::WriteAllBytes($OutputPath, $icoBytes)

# Deterministic ICO structure validation.
$check = [IO.File]::ReadAllBytes($OutputPath)
if ([BitConverter]::ToUInt16($check,0) -ne 0 -or [BitConverter]::ToUInt16($check,2) -ne 1) { throw 'Invalid ICO header.' }
if ([BitConverter]::ToUInt16($check,4) -ne $sizes.Count) { throw 'ICO frame-count mismatch.' }

for ($i = 0; $i -lt $sizes.Count; $i++) {
    $entry = 6 + (16 * $i)
    $expected = [int]$sizes[$i]
    $w = if ($check[$entry] -eq 0) { 256 } else { [int]$check[$entry] }
    $h = if ($check[$entry + 1] -eq 0) { 256 } else { [int]$check[$entry + 1] }
    $bytes = [BitConverter]::ToUInt32($check, $entry + 8)
    $imageOffset = [BitConverter]::ToUInt32($check, $entry + 12)
    if ($w -ne $expected -or $h -ne $expected) { throw "ICO size mismatch at frame $i." }
    if (($imageOffset + $bytes) -gt $check.Length) { throw "ICO frame $i exceeds file bounds." }
    if ([BitConverter]::ToUInt32($check,[int]$imageOffset) -ne 40) { throw "Invalid DIB frame at ${expected}px." }
    if ([BitConverter]::ToInt32($check,[int]$imageOffset + 4) -ne $expected) { throw "Invalid DIB width at ${expected}px." }
    if ([BitConverter]::ToInt32($check,[int]$imageOffset + 8) -ne ($expected * 2)) { throw "Invalid DIB height at ${expected}px." }
}

Write-Host "Generated StarCap ICO: $OutputPath"
Write-Host "Tray optical zoom: 16px=112%, 20px=110%, 24px=108%; 32px+=100%"
Write-Host "Frames: $($sizes -join ', ')"
