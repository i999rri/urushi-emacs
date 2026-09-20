# What the window actually looks like, since the screen it draws is
# built in Lisp and the only way to tell whether it came out right is
# to look at it.
#
#   pwsh -File scripts/verify/capture-window.ps1 -Title urusi-emacs -Out shot.png

param(
    [string] $Title = 'urusi-emacs',
    [Parameter(Mandatory = $true)] [string] $Out
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

    // The frame a window is drawn in, which is smaller than the one it
    // is placed in: the rest is the shadow.
    public const int ExtendedFrameBounds = 9;
}
'@

# By the process rather than by FindWindow: a WinUI window answers to a
# class of its own and the title is set after it is made.
$owner = Get-Process | Where-Object { $_.MainWindowTitle -eq $Title } | Select-Object -First 1
if (-not $owner) {
    throw "no window titled '$Title'"
}

$window = $owner.MainWindowHandle

[Capture]::SetForegroundWindow($window) | Out-Null
Start-Sleep -Milliseconds 300

$bounds = New-Object Capture+RECT
$size = [Runtime.InteropServices.Marshal]::SizeOf($bounds)
if ([Capture]::DwmGetWindowAttribute($window, [Capture]::ExtendedFrameBounds, [ref] $bounds, $size) -ne 0) {
    [Capture]::GetWindowRect($window, [ref] $bounds) | Out-Null
}

$width = $bounds.Right - $bounds.Left
$height = $bounds.Bottom - $bounds.Top
$shot = New-Object Drawing.Bitmap $width, $height
$canvas = [Drawing.Graphics]::FromImage($shot)
$canvas.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $shot.Size)
$shot.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose()
$shot.Dispose()

Write-Output "$Out ($width x $height)"
