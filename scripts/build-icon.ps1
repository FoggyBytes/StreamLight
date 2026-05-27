param(
    [string]$Source       = 'C:\Users\marce\source\repos\StreamLight\app\streamlight.ico',
    [string]$Out          = 'C:\Users\marce\source\repos\StreamLight\app\streamlight.ico',
    [string]$OutInstaller = 'C:\Users\marce\source\repos\StreamLight\installer\resources\streamlight.ico',
    [string]$TopHex       = '#151515',
    [string]$BottomHex    = '#0A3B22',
    [switch]$Preview
)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function Read-IcoFrames {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $frames = @()
    $entryOff = 6
    for ($i = 0; $i -lt $count; $i++) {
        $w = $bytes[$entryOff]
        if ($w -eq 0) { $w = 256 }
        $h = $bytes[$entryOff + 1]
        if ($h -eq 0) { $h = 256 }
        $dataOff  = [BitConverter]::ToUInt32($bytes, $entryOff + 12)
        $bpp      = [BitConverter]::ToUInt16($bytes, $dataOff + 14)
        if ($bpp -ne 32) {
            throw "Frame $i is $bpp bpp (only 32bpp supported)."
        }
        # DIB pixel data (XOR mask) starts at dataOff + 40 (BITMAPINFOHEADER)
        $pixelBytes = $w * $h * 4
        $pixels = New-Object byte[] $pixelBytes
        [Array]::Copy($bytes, $dataOff + 40, $pixels, 0, $pixelBytes)

        # Build a self-contained BMP file in memory for System.Drawing.Bitmap to consume.
        # Use real biHeight (h), bottom-up, BGRA — matches what .ico stores in the XOR region.
        $ms = New-Object System.IO.MemoryStream
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([byte[]]@(0x42, 0x4D))            # 'BM'
        $bw.Write([uint32](14 + 40 + $pixelBytes))  # file size
        $bw.Write([uint16]0); $bw.Write([uint16]0)  # reserved
        $bw.Write([uint32]54)                       # off-bits
        $bw.Write([uint32]40)                       # biSize
        $bw.Write([int32]$w)                        # biWidth
        $bw.Write([int32]$h)                        # biHeight (real)
        $bw.Write([uint16]1)                        # planes
        $bw.Write([uint16]32)                       # bpp
        $bw.Write([uint32]0)                        # BI_RGB
        $bw.Write([uint32]$pixelBytes)              # sizeImage
        $bw.Write([int32]2835); $bw.Write([int32]2835)
        $bw.Write([uint32]0); $bw.Write([uint32]0)
        $bw.Write($pixels)
        $bw.Flush()
        $ms.Position = 0
        $tmp = [System.Drawing.Bitmap]::new($ms)
        # Clone to detach from the stream
        $clone = New-Object System.Drawing.Bitmap $tmp.Width, $tmp.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($clone)
        $g.DrawImage($tmp, 0, 0)
        $g.Dispose()
        $tmp.Dispose()
        $ms.Dispose()

        $frames += [pscustomobject]@{ Width = $w; Height = $h; Bitmap = $clone }
        $entryOff += 16
    }
    return ,$frames
}

function New-GradientComposite {
    param(
        [System.Drawing.Bitmap]$Foreground,
        [System.Drawing.Color]$Top,
        [System.Drawing.Color]$Bottom
    )
    $w = $Foreground.Width; $h = $Foreground.Height
    $canvas = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.CompositingMode    = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    # +1 height endpoint avoids the GDI+ "half-pixel" tiling artifact on small icons
    $p1 = New-Object System.Drawing.PointF 0.0, -0.5
    $p2 = New-Object System.Drawing.PointF 0.0, ($h - 0.5)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $p1, $p2, $Top, $Bottom
    $brush.WrapMode = [System.Drawing.Drawing2D.WrapMode]::TileFlipXY
    $g.FillRectangle($brush, $rect)
    $brush.Dispose()

    $g.DrawImage($Foreground, 0, 0, $w, $h)
    $g.Dispose()
    return $canvas
}

function ConvertTo-IcoDib {
    param([System.Drawing.Bitmap]$Bmp)
    $w = $Bmp.Width; $h = $Bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $bd = $Bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $bd.Stride
    $top = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $top, 0, $stride * $h)
    $Bmp.UnlockBits($bd)

    # Flip top-down → bottom-up for BMP storage
    $bottom = New-Object byte[] ($stride * $h)
    for ($y = 0; $y -lt $h; $y++) {
        [Array]::Copy($top, $y * $stride, $bottom, ($h - 1 - $y) * $stride, $stride)
    }

    # AND mask: 1 bpp, all zero, rows padded to 4 bytes
    $maskStride = [int]((($w + 31) -shr 5) * 4)
    $mask = New-Object byte[] ($maskStride * $h)

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    # BITMAPINFOHEADER with biHeight = 2*h (ICO convention: XOR + AND mask)
    $bw.Write([uint32]40)
    $bw.Write([int32]$w)
    $bw.Write([int32]($h * 2))
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]0)
    $bw.Write([uint32]($stride * $h))
    $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    $bw.Write($bottom)
    $bw.Write($mask)
    $bw.Flush()
    return ,$ms.ToArray()
}

function Save-Ico {
    param(
        [System.Object[]]$Frames,
        [string]$Path
    )
    $dibs = @()
    foreach ($f in $Frames) { $dibs += ,(ConvertTo-IcoDib $f.Bitmap) }

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $bw.Write([uint16]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]$Frames.Count)

    $cur = 6 + 16 * $Frames.Count
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        $w = $Frames[$i].Width; $h = $Frames[$i].Height
        $wByte = if ($w -ge 256) { [byte]0 } else { [byte]$w }
        $hByte = if ($h -ge 256) { [byte]0 } else { [byte]$h }
        $bw.Write($wByte)
        $bw.Write($hByte)
        $bw.Write([byte]0)
        $bw.Write([byte]0)
        $bw.Write([uint16]1)
        $bw.Write([uint16]32)
        $bw.Write([uint32]$dibs[$i].Length)
        $bw.Write([uint32]$cur)
        $cur += $dibs[$i].Length
    }
    foreach ($d in $dibs) { $bw.Write($d) }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
    $ms.Dispose()
    return (Get-Item $Path).Length
}

# === Main ===
Write-Host "Loading source: $Source"
$frames = Read-IcoFrames -Path $Source
Write-Host ("Loaded {0} frames: {1}" -f $frames.Count, (($frames | ForEach-Object { "$($_.Width)x$($_.Height)" }) -join ', '))

$top    = [System.Drawing.ColorTranslator]::FromHtml($TopHex)
$bottom = [System.Drawing.ColorTranslator]::FromHtml($BottomHex)
Write-Host ("Gradient: top={0} -> bottom={1}" -f $TopHex, $BottomHex)

$composed = @()
foreach ($f in $frames) {
    $canvas = New-GradientComposite -Foreground $f.Bitmap -Top $top -Bottom $bottom
    $composed += [pscustomobject]@{ Width = $f.Width; Height = $f.Height; Bitmap = $canvas }
}

if ($Preview) {
    $previewDir = Join-Path (Split-Path $Out -Parent) 'icon-preview'
    if (-not (Test-Path $previewDir)) { New-Item -ItemType Directory -Path $previewDir | Out-Null }
    foreach ($c in $composed) {
        $p = Join-Path $previewDir ("preview-{0}.png" -f $c.Width)
        $c.Bitmap.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "  preview: $p"
    }
}

$bytesA = Save-Ico -Frames $composed -Path $Out
Write-Host ("Wrote {0} bytes -> {1}" -f $bytesA, $Out)
if ($OutInstaller -and ($OutInstaller -ne $Out)) {
    Copy-Item -Path $Out -Destination $OutInstaller -Force
    Write-Host ("Mirrored to            -> {0}" -f $OutInstaller)
}

foreach ($f in $frames)   { $f.Bitmap.Dispose() }
foreach ($c in $composed) { $c.Bitmap.Dispose() }
Write-Host "Done."
