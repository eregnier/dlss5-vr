param(
    [switch]$ForceClose,
    [string]$GameDir = "D:\Games\AFOP"
)

if ($ForceClose) {
    Get-Process afop -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
}

$src = Join-Path $PSScriptRoot "dxgi.dll"
$dst = Join-Path $GameDir "dxgi.dll"

if (-not (Test-Path $src)) {
    Write-Error "[DEPLOY] ERROR: $src introuvable. Lancez build.bat d'abord."
    exit 1
}

try {
    Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop
    Write-Output "[DEPLOY] SUCCESS: $src -> $dst"
} catch {
    Write-Output "[DEPLOY] LOCKED: $dst est verrouille par un processus en cours (ex: afop.exe)."
}
