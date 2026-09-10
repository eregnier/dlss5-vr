@echo off
setlocal
cd /d "%~dp0"

echo ======================================================================
echo  Deploying OptiScaler Pre-SR DLSS 5 for Luke Ross REAL VR
echo ======================================================================

set "TARGET_DIR=%~1"
if "%TARGET_DIR%"=="" set "TARGET_DIR=C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy_optiscaler_vr.ps1" -GameDir "%TARGET_DIR%" -Mode "dbghelp" -WorkingScale 0.75

echo.
echo Process complete.
pause
endlocal
