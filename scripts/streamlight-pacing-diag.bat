@echo off
REM ---------------------------------------------------------------------------
REM Launches StreamLight with the frame-pacing diagnostic switched on.
REM
REM What it does: STREAMLIGHT_PACING_DIAG=1 makes the D3D11 renderer read the
REM presentation statistics back from DXGI on every frame and write one summary
REM line per second to the log (%TEMP%\StreamLight-*.log), tagged [pacing]. With
REM the performance overlay on, the "Frame pacing" line also grows the measured
REM cadence next to the requested one. Off, none of this exists.
REM
REM Only meaningful on the hardware-paced path, which needs Fullscreen + V-Sync +
REM Frame pacing set to Automatic or Hardware + a refresh rate that is an integer
REM multiple (>=2x) of the stream frame rate. The log says so at startup:
REM   "Hardware frame pacing enabled: holding each frame for 2 V-blanks"
REM
REM Lives here rather than next to the exe because build-arch.bat wipes the
REM deploy directory on every clean build, and because a .bat sitting in there
REM would be swept into the installer.
REM
REM Usage:  streamlight-pacing-diag.bat [path\to\StreamLight.exe]
REM ---------------------------------------------------------------------------
setlocal
set STREAMLIGHT_PACING_DIAG=1

set "SL_EXE=%~1"
if "%SL_EXE%"=="" if exist "%~dp0StreamLight.exe" set "SL_EXE=%~dp0StreamLight.exe"
if "%SL_EXE%"=="" set "SL_EXE=%~dp0..\build\deploy-x64-release\StreamLight.exe"

if not exist "%SL_EXE%" (
    echo Could not find StreamLight.exe at:
    echo   %SL_EXE%
    echo.
    echo Pass the path as an argument, or copy this file next to the exe.
    exit /b 1
)

echo Starting StreamLight with pacing diagnostics enabled...
echo   %SL_EXE%
start "" "%SL_EXE%"
