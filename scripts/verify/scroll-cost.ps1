# What a scroll costs the window, turned the same amount every time.
#
#   scripts\verify\scroll-cost.ps1 [-Seconds 20] [-Every 60] [-Notches 1]
#
# Measuring by hand measures something different each time: a run
# scrolled harder than the one before looks worse than it is, which is
# how one change here looked like a loss and another like a win when
# neither was. So the wheel is turned by the machine, so many notches
# every so long, and the time each thread of the window spent is read
# over exactly that stretch, under the names the code gave them.
#
# It begins by turning the wheel back until the buffer can go no
# further, so that a run begins where the last one did.
#
# That is not enough to make two runs the same, and this is worth
# knowing before reading anything into a difference: two runs of this,
# one after the other with nothing changed between them, came to 12.9
# and 22.0 per cent. What reaches Emacs through the pointer is not
# steady, and how much each notch draws depends on what is on the
# screen. So this says the order of a thing -- whether the window is
# spending one per cent or twenty -- and not whether a change made it
# five per cent better. For that, measure the part on its own, the way
# tests/core/Window/test_draw_reader_cost.cpp measures the reading.
#
# The window is brought to the front and the pointer moved onto it:
# a WinUI application listens to the pointer rather than to its
# window's messages, so nothing posted reaches it. Both are put back
# afterwards. Do not type while it runs.

param(
    [int] $Seconds = 20,
    [int] $Every = 60,          # milliseconds between notches
    [int] $Notches = 1,         # notches each time
    [int] $Turn = 30,           # notches before it goes the other way
    [string] $Process = 'urusi_emacs'
)

$ErrorActionPreference = 'Stop'

Add-Type -Namespace Win -Name Turn -MemberDefinition @'
[DllImport("user32.dll")]
public static extern bool SetForegroundWindow(IntPtr window);
[DllImport("user32.dll")]
public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")]
public static extern bool GetWindowRect(IntPtr window, out RECT box);
[DllImport("user32.dll")]
public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")]
public static extern bool GetCursorPos(out POINT where);
[DllImport("user32.dll")]
public static extern void mouse_event(uint flags, int x, int y, int data, IntPtr extra);
[DllImport("kernel32.dll")]
public static extern IntPtr OpenThread(uint access, bool inherit, uint id);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern int GetThreadDescription(IntPtr thread, out IntPtr name);
[DllImport("kernel32.dll")]
public static extern bool CloseHandle(IntPtr handle);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
'@

function NameOf([int] $id) {
    $thread = [Win.Turn]::OpenThread(0x0800, $false, [uint32]$id)
    if ($thread -eq [IntPtr]::Zero) { return '' }
    $name = [IntPtr]::Zero
    [void][Win.Turn]::GetThreadDescription($thread, [ref]$name)
    [void][Win.Turn]::CloseHandle($thread)
    if ($name -eq [IntPtr]::Zero) { return '' }
    [Runtime.InteropServices.Marshal]::PtrToStringUni($name)
}

$app = Get-Process $Process
$window = $app.MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "$Process has no window to scroll"
}

$box = New-Object Win.Turn+RECT
[void][Win.Turn]::GetWindowRect($window, [ref]$box)
$wasPointer = New-Object Win.Turn+POINT
[void][Win.Turn]::GetCursorPos([ref]$wasPointer)
$wasFront = [Win.Turn]::GetForegroundWindow()

[void][Win.Turn]::SetForegroundWindow($window)
[void][Win.Turn]::SetCursorPos([int](($box.Left + $box.Right) / 2),
                               [int](($box.Top + $box.Bottom) / 2))
Start-Sleep -Milliseconds 400

# Back to the top, so that what follows is the same each time.
foreach ($back in 1..200) {
    [Win.Turn]::mouse_event(0x0800, 0, 0, 120 * 3, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 5
}
Start-Sleep -Milliseconds 500

$before = @{}
foreach ($t in $app.Threads) { $before[$t.Id] = $t.TotalProcessorTime.TotalSeconds }

$began = Get-Date
$turned = 0
try {
    # Back and forth from the top rather than on and on: a buffer
    # scrolled to its end goes no further, and a run that reached the
    # end would be measuring an Emacs with nothing to do.
    $way = -1
    while (((Get-Date) - $began).TotalSeconds -lt $Seconds) {
        # MOUSEEVENTF_WHEEL: a notch is 120, and away from you is down.
        [Win.Turn]::mouse_event(0x0800, 0, 0, $way * 120 * $Notches, [IntPtr]::Zero)
        $turned++
        if ($turned % $Turn -eq 0) { $way = -$way }
        Start-Sleep -Milliseconds $Every
    }
}
finally {
    [void][Win.Turn]::SetCursorPos($wasPointer.X, $wasPointer.Y)
    if ($wasFront -ne [IntPtr]::Zero) {
        [void][Win.Turn]::SetForegroundWindow($wasFront)
    }
}

$app.Refresh()
$wall = ((Get-Date) - $began).TotalSeconds

$rows = foreach ($t in $app.Threads) {
    $was = if ($before.ContainsKey($t.Id)) { $before[$t.Id] } else { 0 }
    $spent = $t.TotalProcessorTime.TotalSeconds - $was
    if ($spent -gt 0.05) {
        [pscustomobject]@{
            Thread = $t.Id
            Name = NameOf $t.Id
            Percent = [math]::Round($spent / $wall * 100, 1)
        }
    }
}

"{0} notches over {1:N1}s" -f $turned, $wall
$rows | Sort-Object Percent -Descending | Format-Table -AutoSize
"total: {0:N1}%" -f (($rows | Measure-Object Percent -Sum).Sum)
