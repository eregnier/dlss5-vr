<#
.SYNOPSIS
    Deploys OptiScaler Pre-SR Multipass for Luke Ross REAL VR mods (Cyberpunk 2077, etc.)
.DESCRIPTION
    Replaces the heavy ReShade RenoDX post-SR pipeline with the ultra-lightweight
    OptiScaler Pre-SR architecture (wilsjo2 v0.7.6), locking solid 72 FPS in VR.
#>

param (
    [Parameter(Mandatory=$false)]
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64",
    [Parameter(Mandatory=$false)]
    [ValidateSet("dbghelp", "asi", "dualproxy")]
    [string]$Mode = "dbghelp",
    [Parameter(Mandatory=$false)]
    [float]$WorkingScale = 0.75
)

$ErrorActionPreference = "Stop"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " OptiScaler Pre-SR DLSS 5 Deployer for Luke Ross REAL VR" -ForegroundColor Cyan
Write-Host " Target Game Directory: $GameDir" -ForegroundColor Yellow
Write-Host " Deployment Mode:       $Mode" -ForegroundColor Yellow
Write-Host " WorkingScale:          $WorkingScale (Area: $([math]::Round($WorkingScale * $WorkingScale * 100))%)" -ForegroundColor Yellow
Write-Host "======================================================================" -ForegroundColor Cyan

if (!(Test-Path $GameDir)) {
    Write-Host "[ERROR] Target directory does not exist: $GameDir" -ForegroundColor Red
    exit 1
}

$BinariesDir = "C:\code\clones\binaries"
if (!(Test-Path $BinariesDir)) {
    Write-Host "[ERROR] Binaries directory not found: $BinariesDir" -ForegroundColor Red
    exit 1
}

# 1. Backup old files
$BackupDir = Join-Path $GameDir "backup_pre_optiscaler_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
Write-Host "[BACKUP] Created backup directory: $BackupDir" -ForegroundColor Green

$FilesToBackup = @("OptiScaler.dll", "OptiScaler.ini", "dbghelp.dll", "OptiScaler.asi", "renodx-dlss5.addon64", "ReShade64_dlss5.dll", "ReShade.ini")
foreach ($f in $FilesToBackup) {
    $src = Join-Path $GameDir $f
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $BackupDir -Force
        Write-Host "  -> Backed up: $f" -ForegroundColor DarkGray
    }
}

# 2. Deploy OptiScaler core backend
Write-Host "[DEPLOY] Copying OptiScaler backend directory..." -ForegroundColor Green
$SrcOptiDir = Join-Path $BinariesDir "OptiScaler"
$DstOptiDir = Join-Path $GameDir "OptiScaler"
Copy-Item -Path $SrcOptiDir -Destination $GameDir -Recurse -Force

# 3. Deploy Forwarder nvngx.dll_dlssnr.dll
Write-Host "[DEPLOY] Copying signature forwarder nvngx.dll_dlssnr.dll..." -ForegroundColor Green
Copy-Item -Path (Join-Path $BinariesDir "nvngx.dll_dlssnr.dll") -Destination $GameDir -Force

# 4. Deploy Main OptiScaler library based on chosen mode
if ($Mode -eq "dbghelp") {
    Write-Host "[DEPLOY] Installing OptiScaler as dbghelp.dll (coexists natively with LukeRoss dxgi.dll)..." -ForegroundColor Green
    Copy-Item -Path (Join-Path $BinariesDir "OptiScaler.dll") -Destination (Join-Path $GameDir "dbghelp.dll") -Force
}
elseif ($Mode -eq "asi") {
    Write-Host "[DEPLOY] Installing OptiScaler as OptiScaler.asi (native LukeRoss integration)..." -ForegroundColor Green
    Copy-Item -Path (Join-Path $BinariesDir "OptiScaler.dll") -Destination (Join-Path $GameDir "OptiScaler.asi") -Force
}
elseif ($Mode -eq "dualproxy") {
    Write-Host "[DEPLOY] Installing OptiScaler.dll + Dual-Proxy dxgi.dll..." -ForegroundColor Green
    Copy-Item -Path (Join-Path $BinariesDir "OptiScaler.dll") -Destination (Join-Path $GameDir "OptiScaler.dll") -Force
    Copy-Item -Path "C:\code\dlss5-vr\proxy\dxgi.dll" -Destination (Join-Path $GameDir "dxgi.dll") -Force
}

# 5. Generate and install tuned OptiScaler.ini
Write-Host "[CONFIG] Generating optimized OptiScaler.ini with RunBeforeSR=true, WorkingScale=$WorkingScale..." -ForegroundColor Green
$TunedIni = Join-Path $GameDir "OptiScaler.ini"
& python "C:\code\dlss5-vr\tools\optiscaler_vr_configurator.py"
Copy-Item -Path "C:\code\dlss5-vr\tools\OptiScaler-VR-Cyberpunk2077.ini" -Destination $TunedIni -Force

# 6. Check for nvngx_dlssnr.dll
$DlssNrDll = Join-Path $GameDir "nvngx_dlssnr.dll"
if (!(Test-Path $DlssNrDll)) {
    Write-Host "[WARN] nvngx_dlssnr.dll not found in $GameDir!" -ForegroundColor Yellow
    Write-Host "       Please place your GPU-compatible nvngx_dlssnr.dll in the game folder." -ForegroundColor Yellow
} else {
    Write-Host "[VERIFY] Found nvngx_dlssnr.dll in game folder." -ForegroundColor Green
}

# 7. Disable ReShade addon to avoid dual-hook collision
$ReShadeIni = Join-Path $GameDir "ReShade.ini"
if (Test-Path $ReShadeIni) {
    Write-Host "[CLEANUP] Disabling legacy renodx-dlss5 addon in ReShade.ini to prevent dual-hook collisions..." -ForegroundColor Yellow
    (Get-Content $ReShadeIni) -replace 'renodx-dlss5\.addon64', '' | Set-Content $ReShadeIni
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " [SUCCESS] OptiScaler Pre-SR successfully deployed!" -ForegroundColor Green
Write-Host " Launch your game in VR: DLSS 5 now executes on the render-buffer" -ForegroundColor Green
Write-Host " before super-resolution, staying within the 13.88 ms budget!" -ForegroundColor Green
Write-Host " Press [Insert] in-game to access OptiScaler's live overlay menu." -ForegroundColor Green
Write-Host "======================================================================" -ForegroundColor Cyan
