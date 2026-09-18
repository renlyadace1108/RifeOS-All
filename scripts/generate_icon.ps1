Add-Type -AssemblyName System.Drawing

$assetsDir = Join-Path $PSScriptRoot "..\assets"
if (-not (Test-Path $assetsDir)) {
    New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null
}

function Draw-RifeIcon([int]$sz) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    # Dark obsidian base rounded box
    $pad = [float]($sz * 0.05)
    $boxSz = [float]($sz - 2.0 * $pad)
    $r = [float]($boxSz * 0.22)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [float]($r * 2.0)
    $path.AddArc($pad, $pad, $d, $d, 180, 90)
    $path.AddArc([float]($pad + $boxSz - $d), $pad, $d, $d, 270, 90)
    $path.AddArc([float]($pad + $boxSz - $d), [float]($pad + $boxSz - $d), $d, $d, 0, 90)
    $path.AddArc($pad, [float]($pad + $boxSz - $d), $d, $d, 90, 90)
    $path.CloseFigure()

    # Obsidian Gradient: Deep dark violet to black
    $pt1 = New-Object System.Drawing.PointF(0.0, $pad)
    $pt2 = New-Object System.Drawing.PointF(0.0, [float]($pad + $boxSz))
    $c1 = [System.Drawing.Color]::FromArgb(255, 34, 25, 52)
    $c2 = [System.Drawing.Color]::FromArgb(255, 14, 10, 24)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($pt1, $pt2, $c1, $c2)
    $g.FillPath($brush, $path)
    $brush.Dispose()

    # Subtle inner glowing rim
    $rimW = [Math]::Max(1.0, [float]($sz * 0.025))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 168, 85, 247), $rimW)
    $g.DrawPath($pen, $path)
    $pen.Dispose()

    # Ambient light orb at top right
    $orbPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $orbPath.AddEllipse([float]($sz * 0.45), [float]($sz * 0.08), [float]($sz * 0.45), [float]($sz * 0.45))
    $orbBrush = New-Object System.Drawing.Drawing2D.PathGradientBrush($orbPath)
    $orbBrush.CenterColor = [System.Drawing.Color]::FromArgb(100, 192, 132, 252)
    $orbBrush.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 192, 132, 252))
    $g.FillPath($orbBrush, $orbPath)
    $orbBrush.Dispose()
    $orbPath.Dispose()

    # Draw centered modern 'R'
    $fontSize = [float]($sz * 0.52)
    $font = New-Object System.Drawing.Font('Segoe UI', $fontSize, [System.Drawing.FontStyle]::Bold)
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center

    # Soft text shadow
    $shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(140, 10, 8, 18))
    $rectShadow = New-Object System.Drawing.RectangleF(0.0, [float]($sz * 0.03), [float]$sz, [float]$sz)
    $g.DrawString('R', $font, $shadowBrush, $rectShadow, $sf)
    $shadowBrush.Dispose()

    # Text white foreground
    $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 248, 250, 252))
    $rectText = New-Object System.Drawing.RectangleF(0.0, 0.0, [float]$sz, [float]$sz)
    $g.DrawString('R', $font, $textBrush, $rectText, $sf)
    $textBrush.Dispose()

    $font.Dispose()
    $sf.Dispose()
    $path.Dispose()
    $g.Dispose()

    return $bmp
}

$sizes = @(256, 48, 32, 16)
$pngStreams = @()
foreach ($s in $sizes) {
    $b = Draw-RifeIcon $s
    $ms = New-Object System.IO.MemoryStream
    $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngStreams += ,@($s, $ms.ToArray())
    $ms.Dispose()
    $b.Dispose()
}

$icoPath = Join-Path $assetsDir "rifeos.ico"
if (Test-Path $icoPath) { Remove-Item -Force $icoPath }
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICONDIR header
$bw.Write([uint16]0) # Reserved
$bw.Write([uint16]1) # Type = 1 (Icon)
$bw.Write([uint16]$sizes.Count) # Count

$offset = 6 + 16 * $sizes.Count
foreach ($item in $pngStreams) {
    $s = $item[0]
    $data = $item[1]
    $wByte = if ($s -ge 256) { 0 } else { [byte]$s }
    $hByte = if ($s -ge 256) { 0 } else { [byte]$s }
    $bw.Write([byte]$wByte)
    $bw.Write([byte]$hByte)
    $bw.Write([byte]0) # Color count
    $bw.Write([byte]0) # Reserved
    $bw.Write([uint16]1) # Color planes
    $bw.Write([uint16]32) # Bits per pixel
    $bw.Write([uint32]$data.Length) # Bytes in resource
    $bw.Write([uint32]$offset) # Image offset
    $offset += $data.Length
}

foreach ($item in $pngStreams) {
    $bw.Write($item[1])
}

$bw.Flush()
$bw.Close()
$fs.Close()

Write-Host "Generated $icoPath successfully ($((Get-Item $icoPath).Length) bytes)."
