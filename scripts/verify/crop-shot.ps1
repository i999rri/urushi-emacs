# A piece of a screenshot, enlarged, since what goes wrong in the
# screen is a matter of a few pixels of spacing.
#
#   pwsh -File scripts/verify/crop-shot.ps1 -In shot.png -Out piece.png -X 0 -Y 78 -Width 320 -Height 24 -Scale 4

param(
    [Parameter(Mandatory = $true)] [string] $In,
    [Parameter(Mandatory = $true)] [string] $Out,
    [int] $X = 0,
    [int] $Y = 0,
    [Parameter(Mandatory = $true)] [int] $Width,
    [Parameter(Mandatory = $true)] [int] $Height,
    [int] $Scale = 4
)

Add-Type -AssemblyName System.Drawing

$source = [Drawing.Image]::FromFile((Resolve-Path $In))
$piece = New-Object Drawing.Bitmap ($Width * $Scale), ($Height * $Scale)
$canvas = [Drawing.Graphics]::FromImage($piece)

# Nearest neighbour: the pixels are the evidence.
$canvas.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$canvas.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
$canvas.DrawImage($source,
    (New-Object Drawing.Rectangle 0, 0, ($Width * $Scale), ($Height * $Scale)),
    (New-Object Drawing.Rectangle $X, $Y, $Width, $Height),
    [Drawing.GraphicsUnit]::Pixel)
$piece.Save($Out, [Drawing.Imaging.ImageFormat]::Png)

$canvas.Dispose()
$piece.Dispose()
$source.Dispose()

Write-Output $Out
