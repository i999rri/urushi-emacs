# Posts a mouse message straight to the window of the Emacs frame, the
# way the host passes the mouse on: X and Y count from the corner of the
# frame, in the pixels of the screen. For finding out what Emacs makes
# of it without a pointer on the window.
#
#   scripts/verify/post-mouse.ps1 -Click -X 200 -Y 150
#   scripts/verify/post-mouse.ps1 -Wheel -120 -X 200 -Y 150
#   scripts/verify/post-mouse.ps1 -X 100 -Y 80 -DragToX 400 -DragToY 200
#
# -Child N sends it to the Nth child frame of the root frame instead,
# counting from 1, as the host does for the frame of a panel.

param(
    [int] $X = 0,
    [int] $Y = 0,
    [switch] $Click,
    [int] $Wheel = 0,
    [int] $DragToX = -1,
    [int] $DragToY = -1,
    [int] $Child = 0
)

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class PostMouse {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string className, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr window, uint command);

    public static IntPtr ChildOf(IntPtr parent, int n) {
        IntPtr child = GetWindow(parent, 5);   // GW_CHILD
        for (int i = 1; i < n && child != IntPtr.Zero; i++) child = GetWindow(child, 2);   // GW_HWNDNEXT
        return child;
    }

    public static readonly IntPtr MessageOnly = new IntPtr(-3);

    public static IntPtr FrameOf(uint process) {
        IntPtr window = IntPtr.Zero;
        while ((window = FindWindowExW(MessageOnly, window, "Emacs", null)) != IntPtr.Zero) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == process) return window;
        }
        return IntPtr.Zero;
    }
}
'@

$process = Get-Process urushi_emacs -ErrorAction Stop | Select-Object -First 1
$frame = [PostMouse]::FrameOf([uint32]$process.Id)
if ($frame -eq [IntPtr]::Zero) {
    throw 'no Emacs frame window'
}
if ($Child -gt 0) {
    $frame = [PostMouse]::ChildOf($frame, $Child)
    if ($frame -eq [IntPtr]::Zero) {
        throw "no child frame $Child"
    }
}

$where = [IntPtr](($Y -shl 16) -bor ($X -band 0xffff))
if ($Click) {
    [PostMouse]::PostMessageW($frame, 0x0201, [IntPtr]1, $where) | Out-Null   # WM_LBUTTONDOWN, MK_LBUTTON
    Start-Sleep -Milliseconds 50
    [PostMouse]::PostMessageW($frame, 0x0202, [IntPtr]0, $where) | Out-Null   # WM_LBUTTONUP
    "clicked at $X,$Y"
}
if ($DragToX -ge 0) {
    # Down, a few moves on the way with the button held, and up where
    # it ends, as a hand would drag.
    [PostMouse]::PostMessageW($frame, 0x0201, [IntPtr]1, $where) | Out-Null   # WM_LBUTTONDOWN
    foreach ($step in 1..8) {
        Start-Sleep -Milliseconds 40
        $mx = $X + [int](($DragToX - $X) * $step / 8)
        $my = $Y + [int](($DragToY - $Y) * $step / 8)
        [PostMouse]::PostMessageW($frame, 0x0200, [IntPtr]1, [IntPtr](($my -shl 16) -bor ($mx -band 0xffff))) | Out-Null   # WM_MOUSEMOVE
    }
    Start-Sleep -Milliseconds 40
    $end = [IntPtr](($DragToY -shl 16) -bor ($DragToX -band 0xffff))
    [PostMouse]::PostMessageW($frame, 0x0202, [IntPtr]0, $end) | Out-Null     # WM_LBUTTONUP
    "dragged from $X,$Y to $DragToX,$DragToY"
}
if ($Wheel -ne 0) {
    $w = [IntPtr](([int]$Wheel -shl 16) -band 0xffff0000)
    [PostMouse]::PostMessageW($frame, 0x020A, $w, $where) | Out-Null           # WM_MOUSEWHEEL
    "wheel $Wheel at $X,$Y"
}
