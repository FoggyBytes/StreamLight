#Requires -Version 5.1
<#
.SYNOPSIS
    Build incrementale di StreamLight (release x64).

.DESCRIPTION
    Attiva l'ambiente MSVC, esegue qmake solo se necessario,
    compila con jom e copia l'exe nella deploy directory.

.PARAMETER ForceQmake
    Forza la rigenerazione dei Makefile (utile dopo modifiche a app.pro o resources.qrc).

.PARAMETER Clean
    Esegue una build pulita: cancella build/ e deploy/ prima di ricominciare,
    poi riesegue qmake + jom + windeployqt.

.EXAMPLE
    .\scripts\build-incremental.ps1
    .\scripts\build-incremental.ps1 -ForceQmake
    .\scripts\build-incremental.ps1 -Clean
#>

param(
    [switch]$ForceQmake,
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Percorsi
# ---------------------------------------------------------------------------
$repoRoot   = 'C:\Users\marce\source\repos\StreamLight'
$appPro     = "$repoRoot\app\app.pro"
$buildDir   = "$repoRoot\build\build-x64-release\app"
$deployDir  = "$repoRoot\build\deploy-x64-release"
$exeBuild   = "$buildDir\release\StreamLight.exe"
$exeDeploy  = "$deployDir\StreamLight.exe"
$jom        = "$repoRoot\scripts\jom.exe"
$vswhere    = "$repoRoot\scripts\vswhere.exe"
$qtBin      = 'C:\Qt\6.8.3\msvc2022_64\bin'
$windeployqt = "$qtBin\windeployqt.exe"
$qmlDir     = "$repoRoot\app\gui"

$makefile        = "$buildDir\Makefile"
$makefileRelease = "$buildDir\Makefile.Release"
$makefileDebug   = "$buildDir\Makefile.Debug"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Invoke-OrFail([string]$description, [scriptblock]$block) {
    Write-Host "    $description" -ForegroundColor Gray
    & $block
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERRORE] '$description' ha restituito exit code $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# ---------------------------------------------------------------------------
# 1. Aggiunge Qt e 7-Zip al PATH
# ---------------------------------------------------------------------------
Write-Step "Aggiunta Qt e 7-Zip al PATH"
$env:PATH = "$qtBin;C:\Program Files\7-Zip;$env:PATH"
Write-Host "    qmake : $(& qmake --version 2>&1 | Select-Object -First 1)"

# ---------------------------------------------------------------------------
# 2. Attivazione ambiente MSVC tramite vswhere + vcvars64.bat
# ---------------------------------------------------------------------------
Write-Step "Attivazione ambiente MSVC"
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) {
    Write-Host "[ERRORE] Visual Studio non trovato tramite vswhere." -ForegroundColor Red
    exit 1
}
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
Write-Host "    vcvars64: $vcvars"

# Inietta le variabili di ambiente di MSVC nella sessione PowerShell corrente
$envLines = & cmd /c "`"$vcvars`" > nul 2>&1 && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
Write-Host "    cl.exe  : $(& cl.exe /? 2>&1 | Select-Object -First 1)"

# ---------------------------------------------------------------------------
# 3. Clean opzionale
# ---------------------------------------------------------------------------
if ($Clean) {
    Write-Step "Clean: rimozione build/ e deploy/"
    if (Test-Path "$repoRoot\build\build-x64-release") {
        Remove-Item "$repoRoot\build\build-x64-release" -Recurse -Force
    }
    if (Test-Path $deployDir) {
        Remove-Item $deployDir -Recurse -Force
    }
    $ForceQmake = $true   # clean implica sempre qmake
}

# ---------------------------------------------------------------------------
# 4. qmake (solo se necessario)
# ---------------------------------------------------------------------------
$needQmake = $ForceQmake -or (-not (Test-Path $makefile))

if ($needQmake) {
    Write-Step "qmake — generazione Makefile"

    # Rimuove i Makefile obsoleti se esistono
    Remove-Item $makefile, $makefileRelease, $makefileDebug -ErrorAction SilentlyContinue

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    Push-Location $buildDir
    Invoke-OrFail "qmake $appPro" {
        & qmake $appPro CONFIG+=release CONFIG+=force_debug_info -spec win32-msvc
    }
    Pop-Location
} else {
    Write-Step "qmake saltato (Makefile già presente)"
    Write-Host "    Usa -ForceQmake per rigenerare (dopo modifiche a app.pro o resources.qrc)"
}

# ---------------------------------------------------------------------------
# 5. Compilazione incrementale con jom
# ---------------------------------------------------------------------------
Write-Step "Compilazione con jom (8 thread)"
Push-Location $buildDir
Invoke-OrFail "jom -j8 release" {
    & $jom -j8 release
}
Pop-Location

# ---------------------------------------------------------------------------
# 6. Copia exe nella deploy directory
# ---------------------------------------------------------------------------
Write-Step "Copia StreamLight.exe in deploy/"
if (-not (Test-Path $exeBuild)) {
    Write-Host "[ERRORE] Exe non trovato: $exeBuild" -ForegroundColor Red
    exit 1
}
New-Item -ItemType Directory -Force -Path $deployDir | Out-Null
Copy-Item $exeBuild $exeDeploy -Force
$size = (Get-Item $exeDeploy).Length
Write-Host "    $exeDeploy  ($([math]::Round($size/1MB, 2)) MB)"

# ---------------------------------------------------------------------------
# 7. windeployqt (solo in caso di Clean o deploy/ non ancora popolata)
# ---------------------------------------------------------------------------
$qtDllMarker = "$deployDir\Qt6Core.dll"
if ($Clean -or (-not (Test-Path $qtDllMarker))) {
    Write-Step "windeployqt — deploy Qt DLL"
    Invoke-OrFail "windeployqt" {
        & $windeployqt `
            --release `
            --qmldir $qmlDir `
            --no-opengl-sw --no-compiler-runtime --no-sql `
            --no-system-d3d-compiler --no-system-dxc-compiler `
            '--skip-plugin-types' 'qmltooling,generic' `
            --no-ffmpeg `
            --no-quickcontrols2fusion --no-quickcontrols2imagine --no-quickcontrols2universal `
            --dir $deployDir `
            $exeBuild
    }
} else {
    Write-Step "windeployqt saltato (Qt DLL già presenti in deploy/)"
}

# ---------------------------------------------------------------------------
# Fine
# ---------------------------------------------------------------------------
Write-Host "`n[OK] Build completata." -ForegroundColor Green
Write-Host "     Eseguibile: $exeDeploy"
