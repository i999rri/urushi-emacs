# Posts keys straight to the window of the Emacs frame, the way the host
# passes on the keys it is given, for when the window cannot be brought
# to the front to type into.
#
#   scripts/verify/post-keys.ps1 -Keys Escape,X
#
# Each key is a name of System.Windows.Forms.Keys. Only keys are posted,
# not the state of the modifiers, which Emacs reads from the keyboard:
# M-x is Escape then X.
#
# It is good for opening things and for looking at what opened, not for
# counting what was typed: the keyboard itself says the keys are up, and
# the characters Windows makes of keys it believes are up are sometimes
# made twice. Typing as a person does goes through the host, where the
# keyboard agrees with the keys.

param(
    [Parameter(Mandatory = $true)] [string[]] $Keys,
    [int] $DelayMs = 100
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class PostKeys {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string className, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    public static extern uint MapVirtualKeyW(uint code, uint type);

    public static readonly IntPtr MessageOnly = new IntPtr(-3);

    // The message-only window of the root frame is the first one of the
    // Emacs class that belongs to the process.
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
$frame = [PostKeys]::FrameOf([uint32]$process.Id)
if ($frame -eq [IntPtr]::Zero) {
    throw 'no Emacs frame window'
}

# The message is the one the host posts: the scan code in it as well as
# the key, since without one Windows cannot tell what the key types.
foreach ($name in $Keys) {
    $key = [int][System.Windows.Forms.Keys]$name
    $scan = [long][PostKeys]::MapVirtualKeyW([uint32]$key, 0) -shl 16
    [PostKeys]::PostMessageW($frame, 0x0100, [IntPtr]$key, [IntPtr](1 -bor $scan)) | Out-Null                # WM_KEYDOWN
    [PostKeys]::PostMessageW($frame, 0x0101, [IntPtr]$key, [IntPtr](0xC0000001L -bor $scan)) | Out-Null      # WM_KEYUP
    Start-Sleep -Milliseconds $DelayMs
}
"posted $($Keys -join ' ') to $frame"
