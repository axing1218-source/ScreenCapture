param(
    [Parameter(Mandatory = $false)]
    [string]$OutputPath = "Src\Res\logo.ico"
)

$ErrorActionPreference = 'Stop'

try {
    Add-Type -AssemblyName System.Drawing.Common -ErrorAction Stop
}
catch {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
}

$referenceSize = 1093.0
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)

# Geometry traced from the approved StarCap five-colour mark.
# The transparent gaps between the five points are intentional brand geometry.
$shapes = @(
    [pscustomobject]@{
        Name = 'Blue'
        Color = [System.Drawing.Color]::FromArgb(255, 0, 129, 253)
        Points = @(@(547.0,100.0), @(438.0,418.0), @(541.0,575.0), @(653.0,421.0))
    },
    [pscustomobject]@{
        Name = 'Red'
        Color = [System.Drawing.Color]::FromArgb(255, 249, 49, 50)
        Points = @(@(76.0,425.0), @(353.0,632.0), @(515.0,589.0), @(408.0,426.0))
    },
    [pscustomobject]@{
        Name = 'Green'
        Color = [System.Drawing.Color]::FromArgb(255, 24, 180, 79)
        Points = @(@(1017.0,425.0), @(684.0,426.0), @(577.0,589.0), @(739.0,632.0))
    },
    [pscustomobject]@{
        Name = 'Orange'
        Color = [System.Drawing.Color]::FromArgb(255, 254, 114, 1)
        Points = @(@(528.0,617.0), @(359.0,662.0), @(252.0,991.0), @(530.0,786.0))
    },
    [pscustomobject]@{
        Name = 'Yellow'
        Color = [System.Drawing.Color]::FromArgb(255, 254, 189, 2)
        Points = @(@(564.0,616.0), @(558.0,784.0), @(837.0,992.0), @(732.0,661.0))
    }
)

function New-StarCapBitmap {
    param([int]$Size)

    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )

    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy

        $scale = [double]$Size / $referenceSize

        foreach ($shape in $shapes) {
            $points = [System.Drawing.PointF[]]::new($shape.Points.Count)
            for ($i = 0; $i -lt $shape.Points.Count; $i++) {
                $points[$i] = [System.Drawing.PointF]::new(
                    [float]($shape.Points[$i][0] * $scale),
                    [float]($shape.Points[$i][1] * $scale)
                )
            }

            $brush = [System.Drawing.SolidBrush]::new($shape.Color)
            try {
                $graphics.FillPolygon($brush, $points)
            }
            finally {
                $brush.Dispose()
            }
        }
    }
    finally {
        $graphics.Dispose()
    }

    return $bitmap
}

function Convert-BitmapToIconDib {
    param([System.Drawing.Bitmap]$Bitmap)

    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $andStride = [int](([math]::Floor(($width + 31) / 32)) * 4)
    $andBytes = $andStride * $height
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)

    try {
        # BITMAPINFOHEADER. ICO DIB height is XOR + AND height, hence * 2.
        $writer.Write([uint32]40)
        $writer.Write([int32]$width)
        $writer.Write([int32]($height * 2))
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]0)
        $writer.Write([uint32]($width * $height * 4))
        $writer.Write([int32]0)
        $writer.Write([int32]0)
        $writer.Write([uint32]0)
        $writer.Write([uint32]0)

        # 32-bit BGRA XOR bitmap, bottom-up. Alpha carries smooth edge transparency.
        for ($y = $height - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $width; $x++) {
                $pixel = $Bitmap.GetPixel($x, $y)
                $writer.Write([byte]$pixel.B)
                $writer.Write([byte]$pixel.G)
                $writer.Write([byte]$pixel.R)
                $writer.Write([byte]$pixel.A)
            }
        }

        # Traditional 1-bit AND mask, bottom-up and DWORD-aligned.
        # Fully transparent pixels are masked out; non-zero alpha pixels rely on BGRA alpha.
        for ($y = $height - 1; $y -ge 0; $y--) {
            $maskRow = New-Object byte[] $andStride
            for ($x = 0; $x -lt $width; $x++) {
                if ($Bitmap.GetPixel($x, $y).A -eq 0) {
                    $byteIndex = [int][math]::Floor($x / 8)
                    $bitIndex = 7 - ($x % 8)
                    $maskRow[$byteIndex] = $maskRow[$byteIndex] -bor [byte](1 -shl $bitIndex)
                }
            }
            $writer.Write([byte[]]$maskRow)
        }

        $writer.Flush()
        $bytes = $stream.ToArray()
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }

    $expectedLength = 40 + ($width * $height * 4) + $andBytes
    if ($bytes.Length -ne $expectedLength) {
        throw "Unexpected DIB length for ${width}x${height}: $($bytes.Length), expected $expectedLength."
    }

    return ,$bytes
}

$frames = @()
foreach ($size in $sizes) {
    $bitmap = New-StarCapBitmap -Size $size
    try {
        $frameBytes = Convert-BitmapToIconDib -Bitmap $bitmap
        $frames += [pscustomobject]@{
            Size = $size
            Bytes = $frameBytes
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

$icoStream = [System.IO.MemoryStream]::new()
$icoWriter = [System.IO.BinaryWriter]::new($icoStream)
try {
    # ICONDIR
    $icoWriter.Write([uint16]0)
    $icoWriter.Write([uint16]1)
    $icoWriter.Write([uint16]$frames.Count)

    $offset = 6 + (16 * $frames.Count)

    # ICONDIRENTRY table. Width/height byte value 0 means 256 per ICO spec.
    foreach ($frame in $frames) {
        $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
        $icoWriter.Write([byte]$dimension)
        $icoWriter.Write([byte]$dimension)
        $icoWriter.Write([byte]0)
        $icoWriter.Write([byte]0)
        $icoWriter.Write([uint16]1)
        $icoWriter.Write([uint16]32)
        $icoWriter.Write([uint32]$frame.Bytes.Length)
        $icoWriter.Write([uint32]$offset)
        $offset += $frame.Bytes.Length
    }

    foreach ($frame in $frames) {
        $icoWriter.Write([byte[]]$frame.Bytes)
    }

    $icoWriter.Flush()
    $icoBytes = $icoStream.ToArray()
}
finally {
    $icoWriter.Dispose()
    $icoStream.Dispose()
}

$parent = Split-Path -Parent $OutputPath
if ($parent) {
    New-Item -ItemType Directory -Force $parent | Out-Null
}
[System.IO.File]::WriteAllBytes($OutputPath, $icoBytes)

# Deterministic structural validation before resource compilation.
$check = [System.IO.File]::ReadAllBytes($OutputPath)
if ($check.Length -lt 150) { throw "Generated icon is unexpectedly small: $($check.Length) bytes" }
if ([BitConverter]::ToUInt16($check, 0) -ne 0 -or [BitConverter]::ToUInt16($check, 2) -ne 1) {
    throw 'Generated file does not have a valid ICO header.'
}

$count = [BitConverter]::ToUInt16($check, 4)
if ($count -ne $sizes.Count) {
    throw "Generated ICO frame count mismatch. Expected $($sizes.Count), got $count."
}

for ($i = 0; $i -lt $sizes.Count; $i++) {
    $entryOffset = 6 + (16 * $i)
    $widthByte = [int]$check[$entryOffset]
    $heightByte = [int]$check[$entryOffset + 1]
    $decodedWidth = if ($widthByte -eq 0) { 256 } else { $widthByte }
    $decodedHeight = if ($heightByte -eq 0) { 256 } else { $heightByte }
    $expected = [int]$sizes[$i]

    if ($decodedWidth -ne $expected -or $decodedHeight -ne $expected) {
        throw "ICO directory size mismatch at frame $i. Expected ${expected}x${expected}, got ${decodedWidth}x${decodedHeight}."
    }

    $planes = [BitConverter]::ToUInt16($check, $entryOffset + 4)
    $bpp = [BitConverter]::ToUInt16($check, $entryOffset + 6)
    $bytesInRes = [BitConverter]::ToUInt32($check, $entryOffset + 8)
    $imageOffset = [BitConverter]::ToUInt32($check, $entryOffset + 12)

    if ($planes -ne 1 -or $bpp -ne 32) {
        throw "ICO directory format mismatch at ${expected}px frame."
    }
    if (($imageOffset + $bytesInRes) -gt $check.Length) {
        throw "ICO frame $i points outside the file."
    }

    $dibHeaderSize = [BitConverter]::ToUInt32($check, [int]$imageOffset)
    $dibWidth = [BitConverter]::ToInt32($check, [int]$imageOffset + 4)
    $dibHeight = [BitConverter]::ToInt32($check, [int]$imageOffset + 8)
    $dibPlanes = [BitConverter]::ToUInt16($check, [int]$imageOffset + 12)
    $dibBpp = [BitConverter]::ToUInt16($check, [int]$imageOffset + 14)

    if ($dibHeaderSize -ne 40 -or $dibWidth -ne $expected -or $dibHeight -ne ($expected * 2) -or $dibPlanes -ne 1 -or $dibBpp -ne 32) {
        throw "DIB header mismatch at ${expected}px frame."
    }

    $andStride = [int](([math]::Floor(($expected + 31) / 32)) * 4)
    $expectedFrameBytes = 40 + ($expected * $expected * 4) + ($andStride * $expected)
    if ($bytesInRes -ne $expectedFrameBytes) {
        throw "ICO frame byte count mismatch at ${expected}px. Expected $expectedFrameBytes, got $bytesInRes."
    }
}

Write-Host "Generated StarCap Windows ICO: $OutputPath"
Write-Host "Frames: $($sizes -join ', ')"
Write-Host "Bytes: $($check.Length)"
