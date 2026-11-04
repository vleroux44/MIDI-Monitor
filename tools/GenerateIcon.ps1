param(
    [string]$Source,
    [string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Source) {
    $Source = Join-Path $projectRoot 'assets\MIDIMonitor.png'
}
if (-not $Destination) {
    $Destination = Join-Path $projectRoot 'assets\MIDIMonitor.ico'
}

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
$images = [System.Collections.Generic.List[byte[]]]::new()

try {
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $size,
            $size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        )
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage(
                    $sourceImage,
                    [System.Drawing.Rectangle]::new(0, 0, $size, $size),
                    0,
                    0,
                    $sourceImage.Width,
                    $sourceImage.Height,
                    [System.Drawing.GraphicsUnit]::Pixel
                )
            }
            finally {
                $graphics.Dispose()
            }

            $stream = [System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $images.Add($stream.ToArray())
            }
            finally {
                $stream.Dispose()
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
}
finally {
    $sourceImage.Dispose()
}

$destinationDirectory = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
$file = [System.IO.File]::Open($Destination, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($file)
try {
    $writer.Write([uint16]0)             # Réservé
    $writer.Write([uint16]1)             # Type icône
    $writer.Write([uint16]$sizes.Count)

    $offset = 6 + (16 * $sizes.Count)
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $size = $sizes[$index]
        $encodedSize = if ($size -eq 256) { 0 } else { $size }
        $writer.Write([byte]$encodedSize)
        $writer.Write([byte]$encodedSize)
        $writer.Write([byte]0)           # Palette
        $writer.Write([byte]0)           # Réservé
        $writer.Write([uint16]1)         # Plans
        $writer.Write([uint16]32)        # Profondeur
        $writer.Write([uint32]$images[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($image in $images) {
        $writer.Write($image)
    }
}
finally {
    $writer.Dispose()
    $file.Dispose()
}

Write-Output "Icône générée : $Destination ($($sizes -join ', ') px)"
