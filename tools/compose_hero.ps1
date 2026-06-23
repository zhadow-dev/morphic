# Compose the three real surface captures into one multi-window hero still.
# Pure System.Drawing (no ImageMagick). Real window pixels on a brand-dark canvas.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$assets = Join-Path (Split-Path -Parent $PSScriptRoot) "assets"
$list = [System.Drawing.Image]::FromFile((Join-Path $assets "notes-02-Morphic.png"))  # List
$edit = [System.Drawing.Image]::FromFile((Join-Path $assets "notes-01-Morphic.png"))  # Editor
$insp = [System.Drawing.Image]::FromFile((Join-Path $assets "notes-00-Morphic.png"))  # Inspector

$pad = 90; $gap = 46; $stag = 24
$w = $pad + $list.Width + $gap + $edit.Width + $gap + $insp.Width + $pad
$h = $pad + [Math]::Max($edit.Height, $list.Height) + $pad + $stag
$canvas = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

# brand-dark vertical gradient (#07080C -> #0B0D15)
$rect = New-Object System.Drawing.Rectangle 0,0,$w,$h
$c1 = [System.Drawing.Color]::FromArgb(255,7,8,12)
$c2 = [System.Drawing.Color]::FromArgb(255,12,14,22)
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $c1, $c2, 90.0
$g.FillRectangle($grad, $rect)

function Place($img, $x, $y) {
  # soft drop shadow: stacked translucent offset rects
  for ($i = 10; $i -ge 1; $i--) {
    $a = [int](7 * $i)
    $sb = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($a,0,0,0))
    $g.FillRectangle($sb, $x - $i, $y + $i + 6, $img.Width + 2*$i, $img.Height + $i)
    $sb.Dispose()
  }
  $g.DrawImage($img, $x, $y, $img.Width, $img.Height)
  # hairline rim
  $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(40,255,255,255)), 1
  $g.DrawRectangle($pen, $x, $y, $img.Width-1, $img.Height-1); $pen.Dispose()
}

$x = $pad
Place $list $x ($pad + $stag);                       $x += $list.Width + $gap
Place $edit $x  $pad;                                  $x += $edit.Width + $gap
Place $insp $x ($pad + $stag)

$out = Join-Path $assets "notes-multiwindow.png"
$canvas.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$list.Dispose(); $edit.Dispose(); $insp.Dispose(); $g.Dispose(); $canvas.Dispose()
Write-Host ("saved {0} ({1}x{2})" -f $out, $w, $h)
