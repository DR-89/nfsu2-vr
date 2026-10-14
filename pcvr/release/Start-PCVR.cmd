@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%~dp0pcvr.env") do set "%%A=%%B"
set "NFSU2_GAME_DIR=%~dp0game"
if not exist "%~dp0logs" mkdir "%~dp0logs"
set "NFSU2_PCVR_STDOUT=%~dp0logs\stdout.log"
set "NFSU2_PCVR_STDERR=%~dp0logs\stderr.log"
if exist "%ProgramFiles%\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json" set "XR_RUNTIME_JSON=%ProgramFiles%\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json"
cd /d "%~dp0logs"
"%~dp0nfsu2-pcvr.exe"
if errorlevel 1 (
    echo PCVR exited with an error. See logs\stderr.log next to this launcher.
    pause
)
