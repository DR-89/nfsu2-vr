@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%~dp0pcvr.env") do set "%%A=%%B"
set "NFSU2_GAME_DIR=%~dp0game"
if not exist "%~dp0logs" mkdir "%~dp0logs"
set "NFSU2_PCVR_STDOUT=%~dp0logs\stdout.log"
set "NFSU2_PCVR_STDERR=%~dp0logs\stderr.log"
if /i "%~1"=="-SteamVR" (
    set "XR_RUNTIME_JSON="
    for /f "usebackq delims=" %%R in (`powershell.exe -NoProfile -Command "$p = Join-Path $env:LOCALAPPDATA 'openvr/openvrpaths.vrpath'; foreach ($r in (Get-Content -Raw -LiteralPath $p | ConvertFrom-Json).runtime) { $m = Join-Path $r 'steamxr_win64.json'; if (Test-Path -LiteralPath $m) { $m; break } }"`) do set "XR_RUNTIME_JSON=%%R"
    if not defined XR_RUNTIME_JSON (
        echo Start SteamVR with the headset connected, then run this launcher again.
        pause
        exit /b 1
    )
)
cd /d "%~dp0logs"
"%~dp0nfsu2-pcvr.exe"
if errorlevel 1 (
    echo PCVR exited with an error. See logs\stderr.log next to this launcher.
    pause
)
