# Generates resources/app_icon.ico (rupee sign on a spiral notebook).
# Every size is drawn from vectors with GDI+, so small sizes stay crisp.
# Usage: powershell -ExecutionPolicy Bypass -File resources\make_icon.ps1 [previewPngPath]
param([string]$PreviewPath = "")

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

function Color([string]$hex, [int]$alpha = 255) {
    $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
    return [System.Drawing.Color]::FromArgb($alpha, $c.R, $c.G, $c.B)
}

function RoundedRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($size / 256.0, $size / 256.0)
    $small = $size -le 32

    # Pages peeking out behind the cover
    $pages = RoundedRect 74 30 156 204 18
    $g.FillPath((New-Object System.Drawing.SolidBrush (Color "#E2E8F0")), $pages)
    $g.DrawPath((New-Object System.Drawing.Pen (Color "#94A3B8"), 4), $pages)

    # Cover with a diagonal blue gradient
    $cover = RoundedRect 52 20 164 212 22
    $rect = New-Object System.Drawing.RectangleF 52, 20, 164, 212
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, (Color "#4C9AFF"), (Color "#1D4ED8"), 45.0
    $g.FillPath($grad, $cover)

    # Darker spine band on the left, clipped to the cover shape
    $g.SetClip($cover)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Color "#1E3A8A" 230)), 52, 20, 34, 212)
    $g.ResetClip()

    # Green bookmark ribbon (skipped at tiny sizes)
    if ($size -gt 24) {
        $ribbon = New-Object System.Drawing.Drawing2D.GraphicsPath
        $ribbon.AddPolygon([System.Drawing.PointF[]]@(
            (New-Object System.Drawing.PointF 174, 20), (New-Object System.Drawing.PointF 198, 20),
            (New-Object System.Drawing.PointF 198, 76), (New-Object System.Drawing.PointF 186, 64),
            (New-Object System.Drawing.PointF 174, 76)))
        $g.FillPath((New-Object System.Drawing.SolidBrush (Color "#4CC38A")), $ribbon)
    }

    # Spiral rings across the spine
    $rings = if ($small) { 3 } else { 6 }
    $ringPen = New-Object System.Drawing.Pen (Color "#E5E7EB"), ($(if ($small) { 12 } else { 7 }))
    for ($i = 0; $i -lt $rings; $i++) {
        $y = 46 + $i * (160.0 / ($rings - 1))
        $h = if ($small) { 22 } else { 16 }
        $g.DrawEllipse($ringPen, 36, $y - $h / 2, 34, $h)
    }

    # Rupee sign, measured as a path so it is optically centred on the cover
    $family = New-Object System.Drawing.FontFamily "Segoe UI"
    $glyph = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glyph.AddString([string][char]0x20B9, $family, [int][System.Drawing.FontStyle]::Bold, 150,
                     (New-Object System.Drawing.PointF 0, 0), [System.Drawing.StringFormat]::GenericTypographic)
    $b = $glyph.GetBounds()
    $targetH = if ($small) { 150.0 } else { 128.0 }
    $scale = $targetH / $b.Height
    $m = New-Object System.Drawing.Drawing2D.Matrix
    $m.Translate(152, 128)
    $m.Scale($scale, $scale)
    $m.Translate(-($b.X + $b.Width / 2), -($b.Y + $b.Height / 2))
    $glyph.Transform($m)

    if (-not $small) {
        $shadow = $glyph.Clone()
        $sm = New-Object System.Drawing.Drawing2D.Matrix
        $sm.Translate(3, 4)
        $shadow.Transform($sm)
        $g.FillPath((New-Object System.Drawing.SolidBrush (Color "#0B1B4D" 90)), $shadow)
    }
    $g.FillPath([System.Drawing.Brushes]::White, $glyph)

    $g.Dispose()
    return $bmp
}

# Classic 32-bit DIB icon entry (BITMAPINFOHEADER + bottom-up BGRA + AND mask),
# readable by every Windows API; PNG entries are only used for 256 px.
function ConvertTo-DibBytes($bmp) {
    $s = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $ms
    $maskStride = [int]([math]::Ceiling($s / 32.0) * 4)
    $w.Write([UInt32]40); $w.Write([Int32]$s); $w.Write([Int32]($s * 2))
    $w.Write([UInt16]1); $w.Write([UInt16]32); $w.Write([UInt32]0)
    $w.Write([UInt32]($s * $s * 4 + $maskStride * $s))
    $w.Write([Int32]0); $w.Write([Int32]0); $w.Write([UInt32]0); $w.Write([UInt32]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $w.Write([byte]$c.B); $w.Write([byte]$c.G); $w.Write([byte]$c.R); $w.Write([byte]$c.A)
        }
    }
    $w.Write((New-Object byte[] ($maskStride * $s)))   # AND mask unused: alpha channel is authoritative
    $w.Flush()
    return , $ms.ToArray()   # comma keeps the byte[] from being unrolled
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    if ($s -ge 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $ms.ToArray()
    } else {
        [byte[]]$bytes = ConvertTo-DibBytes $bmp
    }
    $images += , @{ Size = $s; Bytes = $bytes }
    if ($s -eq 256 -and $PreviewPath) { $bmp.Save($PreviewPath, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
}

# ICO container with PNG-compressed entries (supported since Windows Vista)
$icoPath = Join-Path $PSScriptRoot "app_icon.ico"
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$img.Bytes.Length); $bw.Write([UInt32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) { $bw.Write([byte[]]$img.Bytes) }
$bw.Close()
Write-Host "Wrote $icoPath ($($images.Count) sizes)"
