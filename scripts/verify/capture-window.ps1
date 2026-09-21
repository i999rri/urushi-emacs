# What the window actually looks like, since the screen it draws is
# built in Lisp and the only way to tell whether it came out right is
# to look at it.
#
#   pwsh -File scripts/verify/capture-window.ps1 -Out shot.png [-Title title] [-FromScreen]
#
# Without -Title, the window of the running urusi-emacs, whatever its
# title is: Emacs names it. The window draws itself into the picture,
# so one lying over it is not in the picture; -FromScreen copies what is
# on the screen instead, which is what a person sees, other windows and
# all.

param(
    [string] $Title,
    [Parameter(Mandatory = $true)] [string] $Out,
    [switch] $FromScreen
)

Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class Capture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr FindWindowW(string className, string windowName);

    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out RECT value, int size);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);

    // What the window is drawn with, DirectX included, and not only
    // what GDI would draw of it.
    public const uint RenderFullContent = 2;

    // The frame a window is drawn in, which is smaller than the one it
    // is placed in: the rest is the shadow.
    public const int ExtendedFrameBounds = 9;
}
'@

# By the process rather than by FindWindow: a WinUI window answers to a
# class of its own and the title is set after it is made.
if ($Title) {
    $owner = Get-Process | Where-Object { $_.MainWindowTitle -eq $Title } | Select-Object -First 1
} else {
    $owner = Get-Process urusi_emacs -ErrorAction SilentlyContinue |
        Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
}
if (-not $owner) {
    throw "no window titled '$Title'"
}

$window = $owner.MainWindowHandle

$bounds = New-Object Capture+RECT
$size = [Runtime.InteropServices.Marshal]::SizeOf($bounds)
if ([Capture]::DwmGetWindowAttribute($window, [Capture]::ExtendedFrameBounds, [ref] $bounds, $size) -ne 0) {
    [Capture]::GetWindowRect($window, [ref] $bounds) | Out-Null
}

$width = $bounds.Right - $bounds.Left
$height = $bounds.Bottom - $bounds.Top

if ($FromScreen) {
    [Capture]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 300
    $shot = New-Object Drawing.Bitmap $width, $height
    $canvas = [Drawing.Graphics]::FromImage($shot)
    $canvas.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $shot.Size)
    $canvas.Dispose()
} else {
    # PrintWindow draws the whole window, shadow and all, from its own
    # corner; the frame it is drawn in starts that far in.
    $placed = New-Object Capture+RECT
    [Capture]::GetWindowRect($window, [ref] $placed) | Out-Null
    $whole = New-Object Drawing.Bitmap ($placed.Right - $placed.Left), ($placed.Bottom - $placed.Top)
    $canvas = [Drawing.Graphics]::FromImage($whole)
    $dc = $canvas.GetHdc()
    [Capture]::PrintWindow($window, $dc, [Capture]::RenderFullContent) | Out-Null
    $canvas.ReleaseHdc($dc)
    $canvas.Dispose()
    $area = New-Object Drawing.Rectangle ($bounds.Left - $placed.Left), ($bounds.Top - $placed.Top), $width, $height
    $shot = $whole.Clone($area, $whole.PixelFormat)
    $whole.Dispose()
}

$shot.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
$shot.Dispose()

Write-Output "$Out ($width x $height)"
