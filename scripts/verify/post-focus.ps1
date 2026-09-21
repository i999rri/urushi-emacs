# Tells the Emacs frame that the window has lost the focus, or has it
# again, the way the host does when the window is deactivated and
# activated: by posting the focus messages to the root frame's window.
# The window itself cannot be made to lose and regain the focus from
# here, since Windows keeps a background process from taking it back.
#
#   scripts/verify/post-focus.ps1 -Lost
#   scripts/verify/post-focus.ps1 -Gained

param(
    [switch] $Lost,
    [switch] $Gained
)

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class PostFocus {
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
$frame = [PostFocus]::FrameOf([uint32]$process.Id)
if ($frame -eq [IntPtr]::Zero) {
    throw 'no Emacs frame window'
}

if ($Lost) {
    [PostFocus]::PostMessageW($frame, 0x0008, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_KILLFOCUS
    "posted WM_KILLFOCUS to $frame"
}
if ($Gained) {
    [PostFocus]::PostMessageW($frame, 0x0007, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_SETFOCUS
    "posted WM_SETFOCUS to $frame"
}
