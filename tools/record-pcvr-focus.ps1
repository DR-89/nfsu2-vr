param([ValidateRange(2,120)][int]$Seconds=50, [switch]$Benchmark)
$ErrorActionPreference='Stop'
$pcvrRoot=Split-Path -Parent $PSScriptRoot
$run=Get-Content -LiteralPath (Join-Path $pcvrRoot 'build/pcvr-run/current.json') | ConvertFrom-Json
$expected=(Resolve-Path -LiteralPath (Join-Path $pcvrRoot 'build/pcvr/game/nfsu2-pcvr.exe')).Path
$gameProcess=Get-Process -Id $run.Pid
if($gameProcess.Path -ne $expected) { throw 'The current PID is not this workspace PCVR executable.' }
if($Benchmark -and $Seconds -lt 48) { throw 'A benchmark needs at least 48 seconds of observation.' }
if(-not ('PcvrFocusRecord' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PcvrFocusRecord {
    [StructLayout(LayoutKind.Sequential)]
    public struct Power { public uint Version, ControlMask, StateMask; }
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("kernel32.dll",SetLastError=true)]
    public static extern bool GetProcessInformation(IntPtr process,int kind,ref Power power,uint size);
    public static uint ForegroundPid() {
        uint pid; GetWindowThreadProcessId(GetForegroundWindow(),out pid); return pid;
    }
}
'@
}
$report=Join-Path $pcvrRoot ('reports/pcvr-focus-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $report | Out-Null
$beginOffset=(Get-Item -LiteralPath $run.Log).Length
$beginTime=Get-Date
$rows=[System.Collections.Generic.List[object]]::new()
$timer=[Diagnostics.Stopwatch]::StartNew()
if($Benchmark) {
    $trigger=Join-Path $run.RunDirectory 'benchmark.start'
    if(Test-Path -LiteralPath $trigger) { throw 'A benchmark trigger is already pending.' }
    Set-Content -LiteralPath $trigger -Value 'start' -Encoding ascii
}
# Read process metadata only, once per second, off the game's frame path.
# Never activate windows, change scheduling or collect window titles/content.
while($timer.Elapsed.TotalSeconds -lt $Seconds) {
    $gameProcess.Refresh()
    if($gameProcess.HasExited) { break }
    $foreground=[PcvrFocusRecord]::ForegroundPid()
    $kind='other'
    if($foreground -eq $gameProcess.Id) { $kind='game' }
    elseif($foreground) {
        $owner=Get-Process -Id $foreground -ErrorAction SilentlyContinue
        if($owner -and $owner.ProcessName -like 'VirtualDesktop.*') { $kind=$owner.ProcessName }
    }
    $power=[PcvrFocusRecord+Power]::new(); $power.Version=1
    $ok=[PcvrFocusRecord]::GetProcessInformation($gameProcess.Handle,4,[ref]$power,12)
    $powerError=if($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
    $rows.Add([pscustomobject]@{
        Time=(Get-Date).ToString('o'); ElapsedMs=[math]::Round($timer.Elapsed.TotalMilliseconds,3)
        ForegroundPid=$foreground; ForegroundKind=$kind
        PowerQuerySucceeded=$ok; PowerError=$powerError
        PowerControlMask=$power.ControlMask; PowerStateMask=$power.StateMask
        LogBytes=(Get-Item -LiteralPath $run.Log).Length
    })
    Start-Sleep -Milliseconds 1000
}
$rows | Export-Csv -LiteralPath (Join-Path $report 'focus.csv') -NoTypeInformation -Encoding utf8
Copy-Item -LiteralPath $run.Log -Destination (Join-Path $report 'runtime.log')
Copy-Item -LiteralPath (Join-Path $pcvrRoot 'pcvr/benchmark.env') -Destination $report
$log=[IO.File]::ReadAllBytes((Join-Path $report 'runtime.log'))
$newLog=[Text.Encoding]::UTF8.GetString($log,[int]$beginOffset,$log.Length-[int]$beginOffset)
$complete=$Benchmark -and $newLog.Contains('[PCVR] benchmark complete:')
if($complete) { Copy-Item -LiteralPath (Join-Path $run.RunDirectory 'benchmark.csv') -Destination $report }
[pscustomobject]@{
    Begin=$beginTime.ToString('o'); End=(Get-Date).ToString('o'); Pid=$gameProcess.Id
    Executable=$expected; ExecutableSHA256=(Get-FileHash -LiteralPath $expected -Algorithm SHA256).Hash
    Log=$run.Log; BeginLogByte=$beginOffset; EndLogByte=$log.Length
    BenchmarkRequested=[bool]$Benchmark; BenchmarkCompleted=[bool]$complete
    Limits='Focus correlation only. No window activation, settings changes or decoder telemetry. Benchmark requires active tracked race; compare matched views.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report 'capture.json') -Encoding utf8
Write-Output "Saved $report; benchmark complete=$complete"
if($Benchmark -and -not $complete) { Write-Warning 'No new completed benchmark; an older CSV was not copied.' }
