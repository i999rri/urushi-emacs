# What the host spends its time on, thread by thread, with the names
# the code gave them.
param([int] $Seconds = 20)

Add-Type -Namespace Win -Name Threads -MemberDefinition @'
[DllImport("kernel32.dll")]
public static extern IntPtr OpenThread(uint access, bool inherit, uint id);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern int GetThreadDescription(IntPtr thread, out IntPtr name);
[DllImport("kernel32.dll")]
public static extern bool CloseHandle(IntPtr handle);
'@

function NameOf([int] $id) {
    $thread = [Win.Threads]::OpenThread(0x0800, $false, [uint32]$id)   # THREAD_QUERY_LIMITED_INFORMATION
    if ($thread -eq [IntPtr]::Zero) { return '' }
    $name = [IntPtr]::Zero
    [void][Win.Threads]::GetThreadDescription($thread, [ref]$name)
    [void][Win.Threads]::CloseHandle($thread)
    if ($name -eq [IntPtr]::Zero) { return '' }
    [Runtime.InteropServices.Marshal]::PtrToStringUni($name)
}

$p = Get-Process urusi_emacs
$before = @{}
foreach ($t in $p.Threads) { $before[$t.Id] = $t.TotalProcessorTime.TotalSeconds }
$t0 = Get-Date
Start-Sleep -Seconds $Seconds
$p.Refresh()
$wall = ((Get-Date) - $t0).TotalSeconds

$rows = foreach ($t in $p.Threads) {
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
$rows | Sort-Object Percent -Descending | Format-Table -AutoSize
"total: {0:N1}%" -f (($rows | Measure-Object Percent -Sum).Sum)
