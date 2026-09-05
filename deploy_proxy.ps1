param([switch]$ForceClose)

if ($ForceClose) {
    Get-Process afop -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
}

$src = "C:\code\vrdlss5\proxy\dxgi.dll"
$dst = "D:\Games\AFOP\dxgi.dll"

try {
    Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop
    Write-Output "[DEPLOY] SUCCESS: proxy/dxgi.dll deploye avec succes vers $dst"
} catch {
    Write-Output "[DEPLOY] LOCKED: $dst est verrouille par un processus en cours (afop.exe)."
}
