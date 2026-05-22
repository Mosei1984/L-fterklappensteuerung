param(
  [string] $ProjectDirectory = (Join-Path $PSScriptRoot 'src\LuefterConfigurator.Host')
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$brandDirectory = Join-Path $ProjectDirectory 'wwwroot\brand'
$iconDirectory = Join-Path $ProjectDirectory 'Assets\logo'
New-Item -ItemType Directory -Force -Path $brandDirectory, $iconDirectory | Out-Null

function New-RoundedRectanglePath {
  param(
    [float] $X,
    [float] $Y,
    [float] $Width,
    [float] $Height,
    [float] $Radius
  )

  $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
  $diameter = $Radius * 2.0
  $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
  $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
  $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
  $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
  $path.CloseFigure()
  return $path
}

function New-FlapPath {
  param(
    [float] $Scale,
    [bool] $Upper
  )

  $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
  if ($Upper) {
    $path.AddBezier(56 * $Scale, 84 * $Scale, 90 * $Scale, 64 * $Scale, 136 * $Scale, 60 * $Scale, 176 * $Scale, 73 * $Scale)
    $path.AddLine(176 * $Scale, 73 * $Scale, 193 * $Scale, 97 * $Scale)
    $path.AddLine(193 * $Scale, 106 * $Scale, 86 * $Scale, 106 * $Scale)
    $path.AddLine(86 * $Scale, 106 * $Scale, 56 * $Scale, 84 * $Scale)
  } else {
    $path.AddBezier(200 * $Scale, 118 * $Scale, 169 * $Scale, 140 * $Scale, 124 * $Scale, 148 * $Scale, 81 * $Scale, 140 * $Scale)
    $path.AddLine(81 * $Scale, 140 * $Scale, 62 * $Scale, 116 * $Scale)
    $path.AddLine(62 * $Scale, 106 * $Scale, 170 * $Scale, 106 * $Scale)
    $path.AddLine(170 * $Scale, 106 * $Scale, 200 * $Scale, 118 * $Scale)
  }
  $path.CloseFigure()
  return $path
}

function Draw-LogoBitmap {
  param([int] $Size)

  $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.Clear([System.Drawing.Color]::Transparent)

  $scale = $Size / 256.0
  $rect = [System.Drawing.RectangleF]::new(0, 0, $Size, $Size)
  $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
    $rect,
    [System.Drawing.Color]::FromArgb(255, 23, 32, 38),
    [System.Drawing.Color]::FromArgb(255, 88, 193, 183),
    135
  )
  $background.InterpolationColors = [System.Drawing.Drawing2D.ColorBlend]@{
    Colors = @(
      [System.Drawing.Color]::FromArgb(255, 23, 32, 38),
      [System.Drawing.Color]::FromArgb(255, 15, 127, 134),
      [System.Drawing.Color]::FromArgb(255, 88, 193, 183)
    )
    Positions = @(0.0, 0.58, 1.0)
  }
  $rounded = New-RoundedRectanglePath -X 0 -Y 0 -Width $Size -Height $Size -Radius (44 * $scale)
  $graphics.FillPath($background, $rounded)

  $graphics.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(245, 247, 250, 251)), 56 * $scale, 39 * $scale, 144 * $scale, 144 * $scale)
  $graphics.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(125, 23, 32, 38)), 75 * $scale, 58 * $scale, 106 * $scale, 106 * $scale)
  $graphics.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(86, 15, 127, 134)), 86 * $scale, 69 * $scale, 84 * $scale, 84 * $scale)

  $blade = [System.Drawing.PointF[]]@(
    [System.Drawing.PointF]::new(74 * $scale, 128 * $scale),
    [System.Drawing.PointF]::new(165 * $scale, 69 * $scale),
    [System.Drawing.PointF]::new(188 * $scale, 90 * $scale),
    [System.Drawing.PointF]::new(96 * $scale, 151 * $scale),
    [System.Drawing.PointF]::new(72 * $scale, 139 * $scale)
  )
  $graphics.FillPolygon([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(246, 238, 251, 251)), $blade)

  $centerPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(230, 247, 250, 251), [Math]::Max(2.0, 9 * $scale))
  $centerPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $centerPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $graphics.DrawLine($centerPen, 82 * $scale, 139 * $scale, 177 * $scale, 78 * $scale)

  $graphics.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 247, 250, 251)), 181 * $scale, 41 * $scale, 34 * $scale, 34 * $scale)
  $graphics.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 15, 127, 134)), 190 * $scale, 50 * $scale, 16 * $scale, 16 * $scale)

  $railBack = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(184, 247, 250, 251), [Math]::Max(2.0, 9 * $scale))
  $railBack.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $railBack.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $graphics.DrawLine($railBack, 47 * $scale, 199 * $scale, 209 * $scale, 199 * $scale)

  $railFront = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 88, 193, 183), [Math]::Max(2.0, 9 * $scale))
  $railFront.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $railFront.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $graphics.DrawLine($railFront, 47 * $scale, 199 * $scale, 131 * $scale, 199 * $scale)

  if ($Size -ge 96) {
    $font = [System.Drawing.Font]::new("Segoe UI", [Math]::Max(9.0, 20 * $scale), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $graphics.DrawString("80 mm", $font, [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(235, 247, 250, 251)), [System.Drawing.RectangleF]::new(0, 208 * $scale, $Size, 28 * $scale), $format)
    $format.Dispose()
    $font.Dispose()
  }

  $graphics.Dispose()
  return $bitmap
}

function Save-Png {
  param(
    [int] $Size,
    [string] $Path
  )

  $bitmap = Draw-LogoBitmap -Size $Size
  try {
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $bitmap.Dispose()
  }
}

function Write-Ico {
  param(
    [int[]] $Sizes,
    [string] $Path
  )

  $images = @()
  foreach ($size in $Sizes) {
    $stream = [System.IO.MemoryStream]::new()
    $bitmap = Draw-LogoBitmap -Size $size
    try {
      $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
      $images += [pscustomobject]@{
        Size = $size
        Bytes = $stream.ToArray()
      }
    } finally {
      $bitmap.Dispose()
      $stream.Dispose()
    }
  }

  $file = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
  $writer = [System.IO.BinaryWriter]::new($file)
  try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)

    $offset = 6 + (16 * $images.Count)
    foreach ($image in $images) {
      $widthByte = if ($image.Size -ge 256) { 0 } else { $image.Size }
      $writer.Write([byte]$widthByte)
      $writer.Write([byte]$widthByte)
      $writer.Write([byte]0)
      $writer.Write([byte]0)
      $writer.Write([uint16]1)
      $writer.Write([uint16]32)
      $writer.Write([uint32]$image.Bytes.Length)
      $writer.Write([uint32]$offset)
      $offset += $image.Bytes.Length
    }

    foreach ($image in $images) {
      $writer.Write([byte[]]$image.Bytes)
    }
  } finally {
    $writer.Dispose()
    $file.Dispose()
  }
}

Save-Png -Size 256 -Path (Join-Path $brandDirectory 'luefterklappen-icon-256.png')
Save-Png -Size 512 -Path (Join-Path $brandDirectory 'luefterklappen-icon-512.png')
Write-Ico -Sizes @(16, 24, 32, 48, 64, 128, 256) -Path (Join-Path $iconDirectory 'luefterklappen.ico')

Write-Host "Logo assets written to $brandDirectory and $iconDirectory"
