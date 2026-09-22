# Lists every window of a process, with the thread that owns it, its
# parent, and whether it has a paint pending: a window of a thread
# waiting in MsgWaitForMultipleObjects that is never validated wakes it
# again at once, and the thread spins.
#
#   scripts\verify\list-thread-windows.ps1 [-Name urusi_emacs]

param([string] $Name = 'urusi_emacs')

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class ThreadWindows
{
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumThreadWindows(uint t, EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] public static extern bool GetUpdateRect(IntPtr h, IntPtr r, bool e);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);

    public static List<IntPtr> Of(uint thread)
    {
        var found = new List<IntPtr>();
        EnumThreadWindows(thread, (h, x) =>
        {
            found.Add(h);
            EnumChildWindows(h, (c, y) => { found.Add(c); return true; }, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

$process = Get-Process $Name
$rows = foreach ($thread in $process.Threads) {
    foreach ($h in [ThreadWindows]::Of($thread.Id)) {
        $class = New-Object Text.StringBuilder 256
        $title = New-Object Text.StringBuilder 256
        [void][ThreadWindows]::GetClassName($h, $class, 256)
        [void][ThreadWindows]::GetWindowText($h, $title, 256)
        $ignored = 0
        [pscustomobject]@{
            Thread  = [ThreadWindows]::GetWindowThreadProcessId($h, [ref]$ignored)
            Window  = '{0:X8}' -f [int64]$h
            Parent  = '{0:X}' -f [int64][ThreadWindows]::GetParent($h)
            Visible = [ThreadWindows]::IsWindowVisible($h)
            Paint   = [ThreadWindows]::GetUpdateRect($h, [IntPtr]::Zero, $false)
            Class   = "$class"
            Title   = "$title"
        }
    }
}
$rows | Sort-Object Thread, Window -Unique | Format-Table -AutoSize
