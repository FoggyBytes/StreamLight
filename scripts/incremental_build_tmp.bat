@echo off
setlocal

REM Activate MSVC environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Add Qt and 7-Zip to PATH
set "PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Program Files\7-Zip;%PATH%"

REM Verify tools
echo --- PRE-CHECK ---
where cl 2>nul && echo [OK] cl found || echo [FAIL] cl NOT found
where qmake 2>nul && echo [OK] qmake found || echo [FAIL] qmake NOT found
where 7z 2>nul && echo [OK] 7z found || echo [FAIL] 7z NOT found

echo --- BUILDING (jom incremental) ---
set "BUILD_DIR=C:\Users\marce\source\repos\StreamLight\build\build-x64-release\app"
set "JOM=C:\Users\marce\source\repos\StreamLight\scripts\jom.exe"

cd /d "%BUILD_DIR%"
"%JOM%" -j8 release
set BUILD_EXIT=%ERRORLEVEL%

echo --- BUILD EXIT CODE: %BUILD_EXIT% ---

if %BUILD_EXIT%==0 (
    echo --- COPYING EXE ---
    copy /Y "%BUILD_DIR%\release\StreamLight.exe" "C:\Users\marce\source\repos\StreamLight\build\deploy-x64-release\StreamLight.exe"
    echo COPY EXIT: %ERRORLEVEL%
) else (
    echo --- BUILD FAILED, skipping copy ---
)

endlocal
exit /b %BUILD_EXIT%
