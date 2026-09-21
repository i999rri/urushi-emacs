# Posts a mouse message straight to the window of the Emacs frame, the
# way the host passes the mouse on: X and Y count from the corner of the
# frame, in the pixels of the screen. For finding out what Emacs makes
# of it without a pointer on the window.
#
#   scripts/verify/post-mouse.ps1 -Click -X 200 -Y 150
#   scripts/verify/post-mouse.ps1 -Wheel -120 -X 200 -Y 150

param(
    [int] $X = 0,
    [int] $Y = 0,
    [switch] $Click,
    [int] $Wheel = 0
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

$process = Get-Process urusi_emacs -ErrorAction Stop | Select-Object -First 1
$frame = [PostMouse]::FrameOf([uint32]$process.Id)
if ($frame -eq [IntPtr]::Zero) {
    throw 'no Emacs frame window'
}

$where = [IntPtr](($Y -shl 16) -bor ($X -band 0xffff))
if ($Click) {
    [PostMouse]::PostMessageW($frame, 0x0201, [IntPtr]1, $where) | Out-Null   # WM_LBUTTONDOWN, MK_LBUTTON
    Start-Sleep -Milliseconds 50
    [PostMouse]::PostMessageW($frame, 0x0202, [IntPtr]0, $where) | Out-Null   # WM_LBUTTONUP
    "clicked at $X,$Y"
}
if ($Wheel -ne 0) {
    $w = [IntPtr](([int]$Wheel -shl 16) -band 0xffff0000)
    [PostMouse]::PostMessageW($frame, 0x020A, $w, $where) | Out-Null           # WM_MOUSEWHEEL
    "wheel $Wheel at $X,$Y"
}
