# Presses a button of the urushi-emacs window through UI Automation, by
# the name Lisp gave it, and prints the window's state before and after.
# UI Automation presses it without the pointer, so it works from a
# window that is not in front, where a click sent from here would not
# arrive; it does not go through the title bar's regions, only through
# the button and what Lisp does about it.
#
#   scripts/verify/invoke-button.ps1 -Name urushi-maximize

param(
    [Parameter(Mandatory = $true)] [string] $Name
)

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class InvokeButton {
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
'@

function Show-State([IntPtr] $window) {
    "maximized=$([InvokeButton]::IsZoomed($window)) minimized=$([InvokeButton]::IsIconic($window))"
}

$process = Get-Process urushi_emacs -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
$window = $process.MainWindowHandle
$root = [System.Windows.Automation.AutomationElement]::FromHandle($window)
$condition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Name)
$button = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
if (-not $button) {
    throw "no element named '$Name'"
}

"before: $(Show-State $window)"
$button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Seconds 1
"after:  $(Show-State $window)"
