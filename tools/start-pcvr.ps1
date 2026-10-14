param([string]$GameDirectory='', [switch]$DesktopOnly, [switch]$Wait, [switch]$Diagnostics, [switch]$Capture,
      [hashtable]$EnvironmentOverrides=@{})
$ErrorActionPreference='Stop'
$pcvrRoot=Split-Path -Parent $PSScriptRoot
$pcvrExe=Join-Path $pcvrRoot 'build/pcvr/game/nfsu2-pcvr.exe'
$pcvrRun=Join-Path $pcvrRoot 'build/pcvr-run'
if(-not (Test-Path -LiteralPath $pcvrExe)) { throw 'Run tools/build-pcvr.ps1 first.' }
if(-not $GameDirectory) { $GameDirectory=Join-Path $pcvrRoot 'game' }
if(Get-Process -Name 'nfsu2-pcvr' -ErrorAction SilentlyContinue) { throw 'NFSU2 PCVR is already running.' }
New-Item -ItemType Directory -Force $pcvrRun | Out-Null
foreach($line in Get-Content (Join-Path $pcvrRoot 'pcvr/benchmark.env')) {
    if($line -match '^([^#=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
}
foreach($entry in $EnvironmentOverrides.GetEnumerator()) {
    if($entry.Key -notmatch '^(NFSU2_|RECOMP_)[A-Z0-9_]+$') { throw "Invalid game option: $($entry.Key)" }
    [Environment]::SetEnvironmentVariable($entry.Key,[string]$entry.Value,'Process')
}
# Missing data is handled by the native first-run ISO importer before OpenXR.
$env:NFSU2_GAME_DIR=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($GameDirectory)
if($Capture) {
    $env:NFSU2_VR_CAPTURE='1'
    New-Item -ItemType Directory -Force (Join-Path $pcvrRun 'frames') | Out-Null
}
if($Diagnostics) {
    $env:RECOMP_NATIVE_CHECK='1'
    $env:RECOMP_APU_TRACE='1'
    $env:NFSU2_VR_CULL_CAPTURE='1'
    New-Item -ItemType Directory -Force (Join-Path $pcvrRun 'frames') | Out-Null
}
if($DesktopOnly) { $env:NFSU2_VR='0'; $env:RECOMP_VK_HEADLESS='0' }
$runtime='C:/Program Files/Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json'
if(Test-Path -LiteralPath $runtime) { $env:XR_RUNTIME_JSON=$runtime }
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$env:NFSU2_PCVR_STDOUT=Join-Path $pcvrRun "stdout-$stamp.log"
$env:NFSU2_PCVR_STDERR=Join-Path $pcvrRun "stderr-$stamp.log"
# The host opens its log files directly; Start-Process stream redirection
# inserts PowerShell-owned pipes and can stall game threads on the CRT lock.
$process=Start-Process -FilePath $pcvrExe -WorkingDirectory $pcvrRun -WindowStyle Hidden -PassThru
[pscustomobject]@{Pid=$process.Id;RunDirectory=$pcvrRun;Log="$pcvrRun/stderr-$stamp.log"} | ConvertTo-Json | Set-Content -Encoding utf8 "$pcvrRun/current.json"
Get-Content "$pcvrRun/current.json"
if($Wait) {
    $process.WaitForExit()
    Write-Output "PCVR exit code: $($process.ExitCode)"
}
