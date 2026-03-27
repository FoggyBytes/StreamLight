param([switch]$CopyOnly)

$ErrorActionPreference = 'Stop'

$env:PATH = 'C:\Qt\6.7.3\msvc2022_64\bin;C:\Program Files\7-Zip;' + $env:PATH

$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

# Capture all environment variables set by vcvars64.bat
# Add the VS installer dir to PATH within the cmd session so vcvars internal
# calls to vswhere.exe (located there) succeed.
$vsInstallerDir = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$cmdLine = "set PATH=$vsInstallerDir;%PATH% && `"$vcvars`" && set"
$envVars = & cmd /c $cmdLine 2>&1
foreach ($line in $envVars) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
Write-Host "MSVC env activated"
Write-Host "cl: $(& where.exe cl 2>&1)"

if ($CopyOnly) {
    Write-Host "--- CopyOnly mode: skipping compile ---"
} else {
    $jom      = 'C:\Users\marce\source\repos\StreamLight\scripts\jom.exe'
    $buildDir = 'C:\Users\marce\source\repos\StreamLight\build\build-x64-release\app'

    Push-Location $buildDir
    try {
        Write-Host "--- Running jom -j8 release ---"
        & $jom -j8 release
        $jomExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($jomExit -ne 0) {
        Write-Error "jom failed with exit code $jomExit"
        exit $jomExit
    }
    Write-Host "--- jom succeeded ---"
}

$src = 'C:\Users\marce\source\repos\StreamLight\build\build-x64-release\app\release\StreamLight.exe'
$dst = 'C:\Users\marce\source\repos\StreamLight\build\deploy-x64-release\StreamLight.exe'
Copy-Item $src $dst -Force
Write-Host "--- EXE copied to deploy directory ---"
Write-Host "DONE"
