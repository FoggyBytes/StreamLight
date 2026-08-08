#Requires -Version 5.1
<#
.SYNOPSIS
    Build release completo di StreamLight — FoggyBytes
    Esegui con doppio clic o da PowerShell senza parametri.

.DESCRIPTION
    1. Aggiunge Qt 6.8.3 e 7-Zip al PATH della sessione
    2. Esegue build-arch.bat release (clean build completo)
       Il bat fallisce volutamente alla fine (flag windeployqt non supportato) — e' normale
    3. Verifica che StreamLight.exe sia stato prodotto
    4. Esegue windeployqt con i flag corretti per Qt 6.8.3
    5. Copia StreamLight.exe e le DLL di libs nella cartella di output finale
    6. Apre la cartella output in Explorer
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Mantiene la finestra aperta su qualsiasi uscita inattesa
trap {
    Write-Host ""
    Write-Host "ERRORE IMPREVISTO: $_" -ForegroundColor Red
    Write-Host ""
    Read-Host "Premi Invio per chiudere"
    exit 1
}

# ---------------------------------------------------------------------------
# Percorsi fissi
# ---------------------------------------------------------------------------
$StreamLightRoot  = 'C:\Users\marce\source\repos\StreamLight'
$QtBinPath        = 'C:\Qt\6.8.3\msvc2022_64\bin'
$SevenZipPath     = 'C:\Program Files\7-Zip'
$WinDeployQt      = Join-Path $QtBinPath 'windeployqt.exe'
$BuildArchBat     = Join-Path $StreamLightRoot 'scripts\build-arch.bat'
$QmlDir           = Join-Path $StreamLightRoot 'app\gui'
$CompiledExe      = Join-Path $StreamLightRoot 'build\build-x64-release\app\release\StreamLight.exe'
$DeployFolder     = Join-Path $StreamLightRoot 'build\deploy-x64-release'
$LibsFolder       = Join-Path $StreamLightRoot 'libs\windows\lib\x64'
$OutputFolder     = Join-Path $StreamLightRoot 'build\release'

# ---------------------------------------------------------------------------
# Funzioni di utilita'
# ---------------------------------------------------------------------------
function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host ">>> $Message" -ForegroundColor Cyan
}

function Write-OK {
    param([string]$Message)
    Write-Host "    OK: $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "    WARN: $Message" -ForegroundColor Yellow
}

function Write-Fail {
    param([string]$Message)
    Write-Host ""
    Write-Host "ERRORE: $Message" -ForegroundColor Red
}

# ---------------------------------------------------------------------------
# Intestazione
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "========================================" -ForegroundColor White
Write-Host "  StreamLight — Build Release Completo" -ForegroundColor White
Write-Host "  FoggyBytes" -ForegroundColor White
Write-Host "========================================" -ForegroundColor White
Write-Host "  Data: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host "  Output: $OutputFolder"

# ---------------------------------------------------------------------------
# Step 0 — Verifica prerequisiti
# ---------------------------------------------------------------------------
Write-Step "Verifica prerequisiti"

if (-not (Test-Path $BuildArchBat)) {
    Write-Fail "build-arch.bat non trovato: $BuildArchBat"
    Read-Host "Premi Invio per chiudere"
    exit 1
}
Write-OK "build-arch.bat trovato"

if (-not (Test-Path $QtBinPath)) {
    Write-Fail "Qt bin path non trovato: $QtBinPath"
    Read-Host "Premi Invio per chiudere"
    exit 1
}
Write-OK "Qt 6.8.3 trovato: $QtBinPath"

if (-not (Test-Path (Join-Path $SevenZipPath '7z.exe'))) {
    Write-Fail "7z.exe non trovato in: $SevenZipPath"
    Read-Host "Premi Invio per chiudere"
    exit 1
}
Write-OK "7z.exe trovato"

if (-not (Test-Path $WinDeployQt)) {
    Write-Fail "windeployqt.exe non trovato: $WinDeployQt"
    Read-Host "Premi Invio per chiudere"
    exit 1
}
Write-OK "windeployqt.exe trovato"

if (-not (Test-Path $LibsFolder)) {
    Write-Warn "Cartella libs non trovata: $LibsFolder (le DLL non verranno copiate)"
    $LibsAvailable = $false
} else {
    Write-OK "libs/windows/lib/x64 trovata"
    $LibsAvailable = $true
}

# ---------------------------------------------------------------------------
# Step 1 — Aggiunge Qt e 7-Zip al PATH della sessione
# ---------------------------------------------------------------------------
Write-Step "Configurazione PATH (Qt + 7-Zip)"

$env:PATH = "$QtBinPath;$SevenZipPath;$env:PATH"
Write-OK "PATH aggiornato per la sessione corrente"

# ---------------------------------------------------------------------------
# Step 2 — Clean build via build-arch.bat
# ---------------------------------------------------------------------------
Write-Step "Avvio clean build (build-arch.bat release)"
Write-Host "    Questo step richiede diversi minuti. Attendere..."
Write-Host "    NOTA: il bat terminera' con un errore su --no-quickcontrols2fluentwinui3styleimpl"
Write-Host "          Questo e' atteso e normale su Qt 6.8.3."

Push-Location $StreamLightRoot
try {
    # Esegue il bat catturando l'output ma stampandolo in tempo reale
    $batProcess = Start-Process -FilePath 'cmd.exe' `
        -ArgumentList "/c `"$BuildArchBat`" release" `
        -WorkingDirectory $StreamLightRoot `
        -NoNewWindow `
        -PassThru `
        -Wait

    $batExitCode = $batProcess.ExitCode
} finally {
    Pop-Location
}

# Il bat fallisce volutamente — verifichiamo che l'exe esista prima di considerarlo un errore reale
if (-not (Test-Path $CompiledExe)) {
    Write-Fail "build-arch.bat ha fallito E StreamLight.exe non e' stato prodotto."
    Write-Fail "Controlla l'output sopra per errori di compilazione reali."
    Write-Fail "Exit code bat: $batExitCode"
    Write-Host ""
    Write-Host "Possibili cause:" -ForegroundColor Yellow
    Write-Host "  - Errori di compilazione C++ (vedi output sopra)" -ForegroundColor Yellow
    Write-Host "  - Submoduli non inizializzati (git submodule update --init --recursive)" -ForegroundColor Yellow
    Write-Host "  - libs/windows/lib/x64/ non popolata" -ForegroundColor Yellow
    Read-Host "Premi Invio per chiudere"
    exit 1
}

Write-OK "StreamLight.exe prodotto correttamente"
Write-Warn "build-arch.bat ha restituito exit code $batExitCode (atteso — step windeployqt del bat ignorato)"

# ---------------------------------------------------------------------------
# Step 3 — Prepara la cartella di output finale
# ---------------------------------------------------------------------------
Write-Step "Preparazione cartella output: $OutputFolder"

if (Test-Path $OutputFolder) {
    Write-Host "    Pulizia cartella output esistente..."
    Remove-Item -Recurse -Force $OutputFolder
}
New-Item -ItemType Directory -Force -Path $OutputFolder | Out-Null
Write-OK "Cartella output creata"

# ---------------------------------------------------------------------------
# Step 4 — Copia le DLL di libs
# ---------------------------------------------------------------------------
Write-Step "Copia DLL di libs (libs/windows/lib/x64)"

if ($LibsAvailable) {
    $libDlls = Get-ChildItem -Path $LibsFolder -Filter '*.dll' -ErrorAction SilentlyContinue
    if ($libDlls.Count -eq 0) {
        Write-Warn "Nessuna DLL trovata in $LibsFolder"
    } else {
        foreach ($dll in $libDlls) {
            Copy-Item $dll.FullName -Destination $OutputFolder -Force
        }
        Write-OK "$($libDlls.Count) DLL copiate da libs"
    }
} else {
    Write-Warn "libs non disponibili — DLL non copiate"
}

# ---------------------------------------------------------------------------
# Step 5 — Copia AntiHooking.dll e gamecontrollerdb.txt da deploy-x64-release
# ---------------------------------------------------------------------------
Write-Step "Copia file aggiuntivi da deploy-x64-release"

$antiHooking = Join-Path $DeployFolder 'AntiHooking.dll'
if (Test-Path $antiHooking) {
    Copy-Item $antiHooking -Destination $OutputFolder -Force
    Write-OK "AntiHooking.dll copiato"
} else {
    Write-Warn "AntiHooking.dll non trovato in $DeployFolder — saltato"
}

$gcDb = Join-Path $DeployFolder 'gamecontrollerdb.txt'
if (Test-Path $gcDb) {
    Copy-Item $gcDb -Destination $OutputFolder -Force
    Write-OK "gamecontrollerdb.txt copiato"
} else {
    Write-Warn "gamecontrollerdb.txt non trovato in $DeployFolder — saltato"
}

# ---------------------------------------------------------------------------
# Step 6 — windeployqt (con flag corretti per Qt 6.8.3)
# ---------------------------------------------------------------------------
Write-Step "Deploy dipendenze Qt (windeployqt)"
Write-Host "    Exe sorgente: $CompiledExe"
Write-Host "    Destinazione: $OutputFolder"

$windeployArgs = @(
    '--release'
    '--qmldir', $QmlDir
    '--no-opengl-sw'
    '--no-compiler-runtime'
    '--no-sql'
    '--no-system-d3d-compiler'
    '--no-system-dxc-compiler'
    '--skip-plugin-types', 'qmltooling,generic'
    '--no-ffmpeg'
    '--no-quickcontrols2fusion'
    '--no-quickcontrols2imagine'
    '--no-quickcontrols2universal'
    '--dir', $OutputFolder
    $CompiledExe
)

& $WinDeployQt @windeployArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail "windeployqt ha restituito exit code $LASTEXITCODE"
    Write-Fail "Controlla l'output sopra per dettagli."
    Read-Host "Premi Invio per chiudere"
    exit 1
}
Write-OK "windeployqt completato"

# ---------------------------------------------------------------------------
# Step 7 — Copia StreamLight.exe nella cartella di output
# (windeployqt non copia l'exe — lo fa questo step)
# ---------------------------------------------------------------------------
Write-Step "Copia StreamLight.exe nell'output finale"

Copy-Item $CompiledExe -Destination $OutputFolder -Force
Write-OK "StreamLight.exe copiato in $OutputFolder"

# ---------------------------------------------------------------------------
# Step 8 — Pulizia directory Qt inutilizzate (speculare a build-arch.bat)
# ---------------------------------------------------------------------------
Write-Step "Pulizia directory Qt non necessarie"

$dirsToRemove = @(
    'qml\QtQuick\Controls\Fusion'
    'qml\QtQuick\Controls\Imagine'
    'qml\QtQuick\Controls\Universal'
    'qml\QtQuick\Controls\Windows'
    'qml\QtQuick\Controls\FluentWinUI3'
    'qml\QtQuick\NativeStyle'
)

foreach ($rel in $dirsToRemove) {
    $fullPath = Join-Path $OutputFolder $rel
    if (Test-Path $fullPath) {
        Remove-Item -Recurse -Force $fullPath
        Write-Host "    Rimosso: $rel"
    }
}
Write-OK "Pulizia completata"

# ---------------------------------------------------------------------------
# Riepilogo finale
# ---------------------------------------------------------------------------
$exeInOutput = Join-Path $OutputFolder 'StreamLight.exe'
$fileCount   = (Get-ChildItem -Recurse -File $OutputFolder).Count

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Build completata: $OutputFolder" -ForegroundColor Green
Write-Host "  File totali nella cartella: $fileCount" -ForegroundColor Green
Write-Host "  Exe: $exeInOutput" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

# Apre la cartella in Explorer
Start-Process 'explorer.exe' -ArgumentList $OutputFolder

Read-Host "Premi Invio per chiudere"
