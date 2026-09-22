# Samples the stack of the busiest thread of the application a few
# times, without stopping it, and names the frames in libemacs.dll with
# addr2line from the unstripped build: what a thread that spins is
# doing, when a debugger is already attached and no other can be.
#
#   scripts\verify\sample-emacs-thread.ps1 [-Samples 5] [-Depth 20]

param([int] $Samples = 5, [int] $Depth = 20, [string] $Name = 'urusi_emacs')

$ErrorActionPreference = 'Stop'
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$here = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dll = Join-Path $here 'external\emacs-build\src\libemacs.dll'
$msys = "$env:USERPROFILE\scoop\apps\msys2\current"
if (-not (Test-Path "$msys\usr\bin\bash.exe")) { $msys = 'C:\msys64' }
$addr2line = "$msys\mingw64\bin\addr2line.exe"
$objdump = "$msys\mingw64\bin\objdump.exe"

$base = [Convert]::ToUInt64((& $objdump -p $dll | Select-String 'ImageBase').ToString().Split()[-1], 16)
$process = Get-Process $Name
$thread = ($process.Threads | Sort-Object TotalProcessorTime -Descending | Select-Object -First 1).Id
"thread $thread"

foreach ($i in 1..$Samples) {
    $frames = & $cdb -pv -p $process.Id -y "" -c "~~[0n$thread]k $Depth; qd" 2>&1 |
        Select-String -Pattern '^[0-9a-f]{8}`[0-9a-f]{8} ' | ForEach-Object { ($_ -split '\s+')[-1] }
    $names = foreach ($frame in $frames) {
        if ($frame -match '^libemacs\+0x([0-9a-f]+)$') {
            # A return address is past the call; one back is in it.
            $address = '0x{0:x}' -f ($base + [Convert]::ToUInt64($Matches[1], 16) - 1)
            $line = & $addr2line -f -e $dll $address
            "$($line[0]) $(Split-Path -Leaf $line[1])"
        } else {
            $frame
        }
    }
    "--- sample $i"
    $names
}
