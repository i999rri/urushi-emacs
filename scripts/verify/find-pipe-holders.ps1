# Lists the processes holding pipes that the process TARGET created.
#
# A program that ends without Emacs seeing the end of its output is a
# pipe that some other process still holds the writing end of.  An
# anonymous pipe is a named pipe underneath, and a named pipe knows the
# process that created it, so asking every pipe handle in the system who
# created its pipe says who is holding on to one of TARGET's.  Anonymous
# pipes have no name to go by here, which rules out doing it the way
# handle.exe would.
#
#   pwsh -File scripts/verify/find-pipe-holders.ps1 -Target <pid>

param(
    [Parameter(Mandatory = $true)] [int] $Target,
    # List the pipes TARGET itself holds, with their names, instead.
    [switch] $Own
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class PipeHolders
{
    [StructLayout(LayoutKind.Sequential)]
    struct HandleEntry
    {
        public IntPtr Object;
        public IntPtr UniqueProcessId;
        public IntPtr HandleValue;
        public uint GrantedAccess;
        public ushort CreatorBackTraceIndex;
        public ushort ObjectTypeIndex;
        public uint HandleAttributes;
        public uint Reserved;
    }

    [DllImport("ntdll.dll")]
    static extern int NtQuerySystemInformation(int cls, IntPtr info, int length, out int returned);

    [DllImport("ntdll.dll")]
    static extern int NtQueryObject(IntPtr handle, int cls, IntPtr info, int length, out int returned);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DuplicateHandle(IntPtr sourceProcess, IntPtr source, IntPtr targetProcess,
                                       out IntPtr target, uint access, bool inherit, uint options);

    [DllImport("kernel32.dll")]
    static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll")]
    static extern uint GetFileType(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetNamedPipeServerProcessId(IntPtr pipe, out uint pid);

    const int SystemExtendedHandleInformation = 64;
    const int ObjectNameInformation = 1;
    const uint ProcessDupHandle = 0x40;
    const uint DuplicateSameAccess = 2;
    const uint FileTypePipe = 3;

    static List<HandleEntry> AllHandles()
    {
        int length = 1 << 20;
        while (true)
        {
            IntPtr buffer = Marshal.AllocHGlobal(length);
            int returned;
            int status = NtQuerySystemInformation(SystemExtendedHandleInformation, buffer, length, out returned);
            if (status == unchecked((int)0xC0000004))  // STATUS_INFO_LENGTH_MISMATCH
            {
                Marshal.FreeHGlobal(buffer);
                length = Math.Max(length * 2, returned + (1 << 16));
                continue;
            }
            if (status != 0)
            {
                Marshal.FreeHGlobal(buffer);
                throw new Exception("NtQuerySystemInformation failed: 0x" + status.ToString("x8"));
            }

            long count = Marshal.ReadIntPtr(buffer).ToInt64();
            int size = Marshal.SizeOf(typeof(HandleEntry));
            IntPtr first = IntPtr.Add(buffer, IntPtr.Size * 2);
            var handles = new List<HandleEntry>((int)count);
            for (long i = 0; i < count; i++)
                handles.Add((HandleEntry)Marshal.PtrToStructure(IntPtr.Add(first, (int)(i * size)), typeof(HandleEntry)));
            Marshal.FreeHGlobal(buffer);
            return handles;
        }
    }

    // The name of a handle, asked for on a thread of its own: asking a
    // synchronous pipe for its name can wait on the pipe.
    static string NameOf(IntPtr handle)
    {
        string name = null;
        var worker = new Thread(() =>
        {
            int length = 4096;
            IntPtr buffer = Marshal.AllocHGlobal(length);
            int returned;
            if (NtQueryObject(handle, ObjectNameInformation, buffer, length, out returned) == 0)
                name = Marshal.PtrToStringUni(Marshal.ReadIntPtr(buffer, IntPtr.Size));
            Marshal.FreeHGlobal(buffer);
        });
        worker.IsBackground = true;
        worker.Start();
        return worker.Join(200) ? name : null;
    }

    // Every pipe handle PID holds, with the name it has, which is what
    // the names of pipes look like on this Windows.
    public static List<string> PipesOf(int pid)
    {
        var found = new List<string>();
        IntPtr self = GetCurrentProcess();
        IntPtr process = OpenProcess(ProcessDupHandle, false, pid);
        if (process == IntPtr.Zero)
            return found;
        foreach (var entry in AllHandles())
        {
            if (entry.UniqueProcessId.ToInt32() != pid)
                continue;
            IntPtr copy;
            if (!DuplicateHandle(process, entry.HandleValue, self, out copy, 0, false, DuplicateSameAccess))
                continue;
            try
            {
                if (GetFileType(copy) != FileTypePipe)
                    continue;
                found.Add(string.Format("0x{0:x}	0x{1:x}	{2}", entry.HandleValue.ToInt64(),
                                        entry.GrantedAccess, NameOf(copy) ?? "(no name)"));
            }
            finally
            {
                CloseHandle(copy);
            }
        }
        CloseHandle(process);
        return found;
    }

    // Which process created the pipe HANDLE is to, asked on a thread of
    // its own for the same reason as the name is.
    static uint CreatorOf(IntPtr handle)
    {
        uint creator = 0;
        var worker = new Thread(() =>
        {
            uint pid;
            if (GetNamedPipeServerProcessId(handle, out pid))
                creator = pid;
        });
        worker.IsBackground = true;
        worker.Start();
        return worker.Join(200) ? creator : 0;
    }

    // Processes whose handles could not be looked at, which is where a
    // holder that does not show up has to be.
    public static SortedSet<int> Unopened = new SortedSet<int>();

    public static List<string> Find(int target)
    {
        var found = new List<string>();
        var opened = new Dictionary<int, IntPtr>();
        IntPtr self = GetCurrentProcess();
        int me = System.Diagnostics.Process.GetCurrentProcess().Id;

        foreach (var entry in AllHandles())
        {
            int pid = entry.UniqueProcessId.ToInt32();
            if (pid == me)
                continue;
            IntPtr process;
            if (!opened.TryGetValue(pid, out process))
            {
                process = OpenProcess(ProcessDupHandle, false, pid);
                opened[pid] = process;
            }
            if (process == IntPtr.Zero)
            {
                Unopened.Add(pid);
                continue;
            }

            IntPtr copy;
            if (!DuplicateHandle(process, entry.HandleValue, self, out copy, 0, false, DuplicateSameAccess))
                continue;
            try
            {
                if (GetFileType(copy) != FileTypePipe)
                    continue;
                if (CreatorOf(copy) == (uint)target)
                    found.Add(string.Format("{0}	0x{1:x}	0x{2:x}	0x{3:x}",
                                            pid, entry.HandleValue.ToInt64(), entry.GrantedAccess,
                                            entry.Object.ToInt64()));
            }
            finally
            {
                CloseHandle(copy);
            }
        }

        foreach (var process in opened.Values)
            if (process != IntPtr.Zero)
                CloseHandle(process);
        return found;
    }
}
'@

if ($Own) {
    [PipeHolders]::PipesOf($Target) | ForEach-Object { $_ -replace '\t', "`t" }
    return
}

$rows = [PipeHolders]::Find($Target)

$unopened = [PipeHolders]::Unopened | ForEach-Object { (Get-Process -Id $_ -ErrorAction SilentlyContinue).ProcessName } |
    Group-Object | ForEach-Object { if ($_.Count -gt 1) { "$($_.Name) x$($_.Count)" } else { $_.Name } }
Write-Output ("could not look inside {0} processes: {1}" -f [PipeHolders]::Unopened.Count, ($unopened -join ', '))

if (-not $rows.Count) {
    Write-Output "no handle anywhere to a pipe that $Target created"
    return
}

$rows | ForEach-Object {
    $holder, $handle, $access, $object = ($_ -replace '\t', "`t") -split "`t"
    $who = (Get-Process -Id $holder -ErrorAction SilentlyContinue).ProcessName
    # Reading is 0x120089 and writing 0x120196 for an anonymous pipe.
    $end = switch ($access) { '0x120089' { 'read' } '0x120196' { 'write' } default { $access } }
    [pscustomobject]@{ Holder = "$holder $who"; Handle = $handle; End = $end; Object = $object }
} | Sort-Object Object, Holder | Format-Table -AutoSize
