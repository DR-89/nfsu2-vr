param([ValidateRange(8,30)][int]$PhaseSeconds=12, [switch]$InspectOnly)
$ErrorActionPreference='Stop'
$pcvrRoot=Split-Path -Parent $PSScriptRoot
$run=Get-Content -LiteralPath (Join-Path $pcvrRoot 'build/pcvr-run/current.json') | ConvertFrom-Json
$expected=(Resolve-Path -LiteralPath (Join-Path $pcvrRoot 'build/pcvr/game/nfsu2-pcvr.exe')).Path
$process=Get-Process -Id $run.Pid
if($process.Path -ne $expected) { throw 'The current PID is not this workspace PCVR executable.' }
if(-not ('PcvrCpuSets' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public sealed class PcvrCpuSets : IDisposable {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessDefaultCpuSets(IntPtr process, [Out] uint[] ids, uint count, out uint required);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetProcessDefaultCpuSets(IntPtr process, uint[] ids, uint count);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetSystemCpuSetInformation(IntPtr info, uint length, out uint returned, IntPtr process, uint flags);
    IntPtr handle;
    public uint[] Original {get; private set;}
    public uint[] Fast {get; private set;}
    public string[] Topology {get; private set;}
    static void Check(bool ok) { if(!ok) throw new System.ComponentModel.Win32Exception(); }
    public PcvrCpuSets(uint pid) {
        handle=OpenProcess(0x1000|0x2000,false,pid);
        if(handle==IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        try {
            uint count;
            bool ok=GetProcessDefaultCpuSets(handle,null,0,out count);
            if(!ok && Marshal.GetLastWin32Error()!=122) Check(false);
            Original=new uint[count];
            Check(GetProcessDefaultCpuSets(handle,Original,count,out count));
            uint length;
            ok=GetSystemCpuSetInformation(IntPtr.Zero,0,out length,handle,0);
            if(!ok && Marshal.GetLastWin32Error()!=122) Check(false);
            IntPtr buffer=Marshal.AllocHGlobal((int)length);
            try {
                Check(GetSystemCpuSetInformation(buffer,length,out length,handle,0));
                var fast=new List<uint>(); var topology=new List<string>();
                int best=-1, worst=256;
                for(int offset=0;offset<length;) {
                    IntPtr p=IntPtr.Add(buffer,offset); int size=Marshal.ReadInt32(p,0);
                    if(size<24 || offset+size>length) throw new InvalidOperationException("Invalid CPU topology record");
                    if(Marshal.ReadInt32(p,4)==0) {
                        uint id=(uint)Marshal.ReadInt32(p,8); byte efficiency=Marshal.ReadByte(p,18), flags=Marshal.ReadByte(p,19);
                        topology.Add(String.Format("id={0} logical={1} core={2} efficiency={3} flags={4}",id,Marshal.ReadByte(p,14),Marshal.ReadByte(p,15),efficiency,flags));
                        // Higher EfficiencyClass identifies the faster class. Do not
                        // select CPUs exclusively allocated to a different process.
                        if((flags&2)==0 || (flags&4)!=0) {
                            worst=Math.Min(worst,efficiency);
                            if(efficiency>best) { best=efficiency; fast.Clear(); }
                            if(efficiency==best) fast.Add(id);
                        }
                    }
                    offset+=size;
                }
                Fast=best>worst?fast.ToArray():new uint[0]; Topology=topology.ToArray();
            } finally { Marshal.FreeHGlobal(buffer); }
        } catch { CloseHandle(handle); handle=IntPtr.Zero; throw; }
    }
    public void Apply(bool fast) {
        uint[] ids=fast?Fast:Original;
        if(fast && ids.Length==0) throw new InvalidOperationException("No distinct faster CPU class");
        Check(SetProcessDefaultCpuSets(handle,ids.Length==0?null:ids,(uint)ids.Length));
    }
    public void Dispose() {
        if(handle!=IntPtr.Zero) { try { Apply(false); } finally { CloseHandle(handle); handle=IntPtr.Zero; } }
    }
}
'@
}
$selection=[PcvrCpuSets]::new([uint32]$run.Pid)
try {
    $selection.Topology
    "Original CPU sets: $($selection.Original -join ','); faster class: $($selection.Fast -join ',')"
    if($InspectOnly) { return }
    if(-not $selection.Fast.Count) { throw 'This CPU has no distinct faster class.' }
    $report=Join-Path $pcvrRoot ('reports/pcvr-cpu-sets-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $report | Out-Null
    $phases=[System.Collections.Generic.List[object]]::new()
    [pscustomobject]@{Pid=$run.Pid;Original=$selection.Original;Fast=$selection.Fast;Topology=$selection.Topology;Log=$run.Log} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$report/settings.json"
    # ABBA, with three seconds excluded after each transition. Log windows
    # crossing either boundary are also excluded during analysis.
    foreach($fast in @($false,$true,$true,$false)) {
        $selection.Apply($fast)
        $phase=$phases.Count+1
        "Phase $phase/4: $(if($fast){'faster CPU class'}else{'original assignment'})"
        $phaseStart=Get-Date
        Start-Sleep -Seconds 3
        $sampleOffset=(Get-Item -LiteralPath $run.Log).Length
        Start-Sleep -Seconds ($PhaseSeconds-3)
        $process.Refresh()
        if($process.HasExited) { throw 'Game exited during comparison.' }
        $phases.Add([pscustomobject]@{Phase=$phase;Fast=$fast;Start=$phaseStart.ToString('o');SampleOffset=$sampleOffset;EndOffset=(Get-Item -LiteralPath $run.Log).Length;End=(Get-Date).ToString('o')})
        $phases | ConvertTo-Json | Set-Content -LiteralPath "$report/phases.json"
    }
    Copy-Item -LiteralPath $run.Log -Destination "$report/runtime.log"
    Copy-Item -LiteralPath (Join-Path $pcvrRoot 'pcvr/benchmark.env') -Destination "$report/benchmark.env"
    "Comparison complete: $report"
} finally {
    $selection.Dispose()
    'Original process CPU-set assignment restored.'
}
