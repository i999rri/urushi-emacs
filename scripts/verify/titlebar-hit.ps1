# Says whether the area a control on the drawn title bar covers is handed
# to the application or taken by Windows as title bar, which is what
# decides whether clicking it does anything at all: a window that draws
# its own title bar tells Windows which parts of it are the bar, and
# every part that is gets the click instead of the control under it.
#
# HTCLIENT (1) is the control's; HTCAPTION (2) is the bar's, and a button
# that hit tests as the bar is a button that cannot be pressed.
#
#   scripts/verify/titlebar-hit.ps1 [-Name urusi-close]

param(
    [string] $Name = 'urusi-close'
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TitleBarHit {
    public struct Point { public int X; public int Y; }
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(
        IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
}
'@

$process = Get-Process urusi_emacs -ErrorAction Stop |
    Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
$root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$condition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Name)
$element = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
if (-not $element) {
    throw "no element named '$Name'"
}

$box = $element.Current.BoundingRectangle
$x = [int]($box.X + $box.Width / 2)
$y = [int]($box.Y + $box.Height / 2)

# The hit test goes to the window under the point, which is the one that
# answers for it: the top window's client area is a child of its own.
$point = New-Object TitleBarHit+Point
$point.X = $x
$point.Y = $y
$under = [TitleBarHit]::WindowFromPoint($point)

$WM_NCHITTEST = 0x0084
$packed = [IntPtr](((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF)))
$hit = [TitleBarHit]::SendMessage($under, $WM_NCHITTEST, [IntPtr]::Zero, $packed).ToInt32()

$what = switch ($hit) {
    1 { 'HTCLIENT: the application gets the click' }
    2 { 'HTCAPTION: Windows takes it as title bar, the button cannot be pressed' }
    default { "$hit" }
}
"$Name at ${x},${y} in window $under -> $what"
