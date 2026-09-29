# Saves a picture of the application's window without bringing it to
# the front:
#
#   scripts\verify\capture-window.ps1 [-Path shot.png]
#
# A screenshot taken the usual way puts another window in front, and
# what is being looked at often goes with it: the input method settles
# what it was composing when the keys leave, and the line the cursor is
# on is drawn differently once the window is not the one in use. This
# asks the window to draw itself into a bitmap instead, which leaves it
# where it is, with the keys and the focus it had.

param(
    [string] $Path = 'urushi-emacs.png',
    [string] $Process = 'urushi_emacs',
    # Photograph it off the screen instead, which is the only way to
    # see what a swap chain shows: it is composed by the desktop, and
    # a window asked to draw itself leaves it out. The window comes to
    # the front for as long as it takes and is put back after.
    [switch] $FromScreen
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class WindowShot
{
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect bounds);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    // Draw everything the window shows, including what the desktop
    // composes for it: without this a window drawn that way comes out
    // blank. It does not reach a swap chain all the same.
    public const uint RenderFullContent = 2;

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
}
'@

$window = (Get-Process -Name $Process -ErrorAction Stop | Select-Object -First 1).MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "$Process has no window to draw"
}

$bounds = New-Object WindowShot+Rect
if (-not [WindowShot]::GetWindowRect($window, [ref] $bounds)) {
    throw 'the window would not say where it is'
}

$width = $bounds.Right - $bounds.Left
$height = $bounds.Bottom - $bounds.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)

if ($FromScreen) {
    $wasFront = [WindowShot]::GetForegroundWindow()
    [void][WindowShot]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 500
    $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0,
                             (New-Object System.Drawing.Size $width, $height))
    if ($wasFront -ne [IntPtr]::Zero) {
        [void][WindowShot]::SetForegroundWindow($wasFront)
    }
    $drawn = $true
    $graphics.Dispose()
}
else {
    $dc = $graphics.GetHdc()
    $drawn = [WindowShot]::PrintWindow($window, $dc, [WindowShot]::RenderFullContent)
    $graphics.ReleaseHdc($dc)
    $graphics.Dispose()
}

if (-not $drawn) {
    $bitmap.Dispose()
    throw 'the window would not draw itself'
}

$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
"$width x $height saved to $Path"
