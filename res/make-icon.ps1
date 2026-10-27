# Regenerates app.ico (32-bit BMP frames + PNG 256px frame). Not needed for building.
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256

function New-Frame([int]$s) {
    $bmp = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded square background.
    [single]$pad = [Math]::Max(0.5, $s * 0.04)
    [single]$w = $s - 2 * $pad
    [single]$d = $s * 0.44
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($pad, $pad, $d, $d, 180, 90)
    $path.AddArc($pad + $w - $d, $pad, $d, $d, 270, 90)
    $path.AddArc($pad + $w - $d, $pad + $w - $d, $d, $d, 0, 90)
    $path.AddArc($pad, $pad + $w - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 37, 99, 235))
    $g.FillPath($brush, $path)

    # White sun: a disc with eight rays (brightness).
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
    [single]$c = $s / 2.0
    [single]$r = $s * 0.16
    $g.FillEllipse($white, $c - $r, $c - $r, 2 * $r, 2 * $r)
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, [single][Math]::Max(1.2, $s * 0.08))
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    for ($i = 0; $i -lt 8; $i++) {
        $a = $i * [Math]::PI / 4
        [single]$x1 = $c + [Math]::Cos($a) * $s * 0.25
        [single]$y1 = $c + [Math]::Sin($a) * $s * 0.25
        [single]$x2 = $c + [Math]::Cos($a) * $s * 0.35
        [single]$y2 = $c + [Math]::Sin($a) * $s * 0.35
        $g.DrawLine($pen, $x1, $y1, $x2, $y2)
    }

    $g.Dispose()
    return $bmp
}

function Get-BmpFrameBytes([System.Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $ms = [System.IO.MemoryStream]::new()
    $bw = [System.IO.BinaryWriter]::new($ms)
    $maskStride = [int]([Math]::Ceiling($s / 32.0) * 4)
    # BITMAPINFOHEADER (height doubled for XOR + AND masks).
    $bw.Write([int]40); $bw.Write([int]$s); $bw.Write([int]($s * 2))
    $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int]0)
    $bw.Write([int]($s * $s * 4 + $maskStride * $s))
    $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    $bw.Write([byte[]]::new($maskStride * $s))
    $bw.Flush()
    return $ms.ToArray()
}

$frames = foreach ($s in $sizes) {
    $bmp = New-Frame $s
    if ($s -ge 256) {
        $ms = [System.IO.MemoryStream]::new()
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $ms.ToArray()
    } else {
        $bytes = Get-BmpFrameBytes $bmp
    }
    $bmp.Dispose()
    [pscustomobject]@{ Size = $s; Bytes = $bytes }
}

$out = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($out)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]$f.Bytes.Length); $bw.Write([int]$offset)
    $offset += $f.Bytes.Length
}
foreach ($f in $frames) { $bw.Write([byte[]]$f.Bytes) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'app.ico'), $out.ToArray())
