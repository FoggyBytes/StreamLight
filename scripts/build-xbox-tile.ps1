param(
    [string]$IcoSource = 'C:\Users\marce\source\repos\StreamLight\app\streamlight.ico',
    [string]$Out       = 'C:\Users\marce\source\repos\StreamLight\app\xbox-tile-preview.png',
    [int]   $Size      = 1024,
    [double]$LogoScale = 0.65,
    [string]$TopHex    = '#151515',
    [string]$BottomHex = '#0A3B22'
)
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

# --- Extract the 256x256 frame from the .ico, preserving alpha ---
$bytes = [System.IO.File]::ReadAllBytes($IcoSource)
$count = [BitConverter]::ToUInt16($bytes, 4)
$frame256 = $null
$entryOff = 6
for ($i = 0; $i -lt $count; $i++) {
    $w = $bytes[$entryOff]; if ($w -eq 0) { $w = 256 }
    $h = $bytes[$entryOff+1]; if ($h -eq 0) { $h = 256 }
    if ($w -eq 256 -and $h -eq 256) {
        $dataOff  = [BitConverter]::ToUInt32($bytes, $entryOff+12)
        $pixelN   = $w * $h * 4
        $pixels   = New-Object byte[] $pixelN
        [Array]::Copy($bytes, $dataOff + 40, $pixels, 0, $pixelN)
        # Build self-contained BMP for System.Drawing
        $ms = New-Object System.IO.MemoryStream
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([byte[]]@(0x42, 0x4D))
        $bw.Write([uint32](14 + 40 + $pixelN)); $bw.Write([uint16]0); $bw.Write([uint16]0)
        $bw.Write([uint32]54); $bw.Write([uint32]40)
        $bw.Write([int32]$w); $bw.Write([int32]$h); $bw.Write([uint16]1); $bw.Write([uint16]32)
        $bw.Write([uint32]0); $bw.Write([uint32]$pixelN); $bw.Write([int32]2835); $bw.Write([int32]2835)
        $bw.Write([uint32]0); $bw.Write([uint32]0); $bw.Write($pixels); $bw.Flush()
        $ms.Position = 0
        $raw = [System.Drawing.Bitmap]::new($ms)
        $frame256 = New-Object System.Drawing.Bitmap $raw.Width, $raw.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g0 = [System.Drawing.Graphics]::FromImage($frame256)
        $g0.DrawImage($raw, 0, 0)
        $g0.Dispose(); $raw.Dispose(); $ms.Dispose()
        break
    }
    $entryOff += 16
}
if (-not $frame256) { throw "256x256 frame not found in $IcoSource" }
Write-Host "Loaded 256x256 frame from .ico (alpha preserved)"

# --- Convert the swoosh's near-black backdrop into transparency.
#     The .ico stores the swoosh as bright pixels on opaque black; we set
#     alpha = max(R,G,B) so dark background fades out and bright glow stays.
function ConvertTo-LuminanceAlpha([System.Drawing.Bitmap]$bmp, [int]$BlackPoint = 6) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0,0,$w,$h
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $bd.Stride
    $buf = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $stride * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + $x * 4
            $b = $buf[$i]; $g = $buf[$i+1]; $r = $buf[$i+2]
            $m = [Math]::Max($r, [Math]::Max($g, $b))
            if ($m -le $BlackPoint) { $a = 0 }
            else {
                # Soft toe: linearly remap [BlackPoint..255] -> [0..255]
                $v = ($m - $BlackPoint) * 255.0 / (255 - $BlackPoint)
                if ($v -gt 255) { $v = 255 }
                $a = [byte][Math]::Round($v)
            }
            $buf[$i+3] = $a
        }
    }
    [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $bd.Scan0, $stride * $h)
    $bmp.UnlockBits($bd)
}

ConvertTo-LuminanceAlpha $frame256
Write-Host "Backdrop converted to transparency (luminance-as-alpha)"

# --- Trim transparent borders so we control the visual padding ---
function Get-OpaqueBounds([System.Drawing.Bitmap]$bmp, [int]$AlphaThreshold = 4) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0,0,$w,$h
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($bd.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $bd.Stride * $h)
    $bmp.UnlockBits($bd)
    $minX = $w; $minY = $h; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $bd.Stride
        for ($x = 0; $x -lt $w; $x++) {
            $a = $buf[$row + $x*4 + 3]
            if ($a -gt $AlphaThreshold) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { return $null }
    return New-Object System.Drawing.Rectangle $minX, $minY, ($maxX-$minX+1), ($maxY-$minY+1)
}

$crop = Get-OpaqueBounds $frame256 12
Write-Host ("Opaque content bounds: {0}x{1} at ({2},{3})" -f $crop.Width, $crop.Height, $crop.X, $crop.Y)

# --- Compose 1024x1024 canvas with gradient ---
$top    = [System.Drawing.ColorTranslator]::FromHtml($TopHex)
$bottom = [System.Drawing.ColorTranslator]::FromHtml($BottomHex)

$canvas = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.CompositingMode    = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
$g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
$g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

$p1 = New-Object System.Drawing.PointF 0.0, -0.5
$p2 = New-Object System.Drawing.PointF 0.0, ($Size - 0.5)
$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $p1, $p2, $top, $bottom
$brush.WrapMode = [System.Drawing.Drawing2D.WrapMode]::TileFlipXY
$g.FillRectangle($brush, 0, 0, $Size, $Size)
$brush.Dispose()

# --- Scale the opaque-cropped logo to occupy LogoScale fraction of the tile, centered ---
$maxDim   = [Math]::Max($crop.Width, $crop.Height)
$targetPx = [int]([Math]::Round($Size * $LogoScale))
$ratio    = $targetPx / $maxDim
$drawW    = [int]([Math]::Round($crop.Width  * $ratio))
$drawH    = [int]([Math]::Round($crop.Height * $ratio))
$dstX     = [int](($Size - $drawW) / 2)
$dstY     = [int](($Size - $drawH) / 2)
$dstRect  = New-Object System.Drawing.Rectangle $dstX, $dstY, $drawW, $drawH

$g.DrawImage($frame256, $dstRect, $crop, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$frame256.Dispose()

# --- Save as PNG ---
$canvas.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose()

$info = Get-Item $Out
Write-Host ""
Write-Host ("OUTPUT: {0}" -f $info.FullName)
Write-Host ("        {0}x{0}  {1:N0} bytes" -f $Size, $info.Length)
