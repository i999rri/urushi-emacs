# Lists the top-level windows of the running urusi-emacs, with whether
# each can be seen and the window it belongs to. A dialog Emacs opens
# is one of them, and it has to belong to the window that is shown:
# one that belongs to the frame's window, which nobody sees, opens
# behind everything else.
#
#   scripts/verify/list-windows.ps1 [-All]
#
# Without -All, only the windows that can be seen.

param(
    [switch] $All
)

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class ListWindows {
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);

    const uint GW_OWNER = 4;

    public static List<string> Of(uint process, bool all) {
        var lines = new List<string>();
        EnumWindows((window, data) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == process && (all || IsWindowVisible(window))) {
                var title = new StringBuilder(256);
                var name = new StringBuilder(256);
                GetWindowText(window, title, title.Capacity);
                GetClassName(window, name, name.Capacity);
                lines.Add(string.Format("{0} visible={1} owner={2} [{3}] {4}",
                    window, IsWindowVisible(window), GetWindow(window, GW_OWNER), name, title));
            }
            return true;
        }, IntPtr.Zero);
        return lines;
    }
}
'@

$process = Get-Process urusi_emacs -ErrorAction Stop | Select-Object -First 1
"main window $($process.MainWindowHandle)"
[ListWindows]::Of([uint32]$process.Id, $All.IsPresent)
