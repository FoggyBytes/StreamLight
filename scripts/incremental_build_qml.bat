@echo off
setlocal

:: Attiva MSVC
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1

:: Aggiungi Qt e 7z al PATH
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Program Files\7-Zip;%PATH%

:: Directory di build
set BUILD_DIR=C:\Users\marce\source\repos\StreamLight\build\build-x64-release\app
set JOM=C:\Users\marce\source\repos\StreamLight\scripts\jom.exe

:: Lancia jom incrementale
cd /d "%BUILD_DIR%"
echo --- Starting jom incremental build ---
"%JOM%" -j8 release
set BUILD_EXIT=%ERRORLEVEL%

echo --- jom exit code: %BUILD_EXIT% ---
exit /b %BUILD_EXIT%
