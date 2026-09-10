@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo  Packaging DLSS 5 ^<^> VR Standalone Release
echo ======================================================================

set "DIST_DIR=dist\DLSS5-VR-Release"
set "ZIP_OUT=dist\DLSS5-VR-Release.zip"

if exist dist rmdir /s /q dist
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\deps"
mkdir "%DIST_DIR%\proxy"

echo.
echo [1/3] Compiling proxy (dxgi.dll)...
cd proxy
call build.bat
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Proxy compilation failed!
    exit /b %ERRORLEVEL%
)
cd ..

echo.
echo [2/3] Compiling universal GUI installer (VR-DLSS5-Installer.exe)...
cd installer
call build.bat
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Installer compilation failed!
    exit /b %ERRORLEVEL%
)
cd ..

echo.
echo [3/3] Assembling standalone distribution package...

copy /y "installer\VR-DLSS5-Installer.exe" "%DIST_DIR%\" >nul
copy /y "proxy\dxgi.dll" "%DIST_DIR%\proxy\" >nul
copy /y "proxy\openvr_api.dll" "%DIST_DIR%\proxy\" >nul
copy /y "deps\OptiScaler.dll" "%DIST_DIR%\deps\" >nul
copy /y "deps\nvngx.dll_dlssnr.dll" "%DIST_DIR%\deps\" >nul
copy /y "deps\OptiScaler.ini" "%DIST_DIR%\deps\" >nul
copy /y "deps\cudart64_12.dll" "%DIST_DIR%\deps\" >nul
if exist "deps\OptiScaler" xcopy /e /i /y "deps\OptiScaler" "%DIST_DIR%\deps\OptiScaler\" >nul
copy /y "LICENSE" "%DIST_DIR%\" >nul
copy /y "README.md" "%DIST_DIR%\" >nul

echo [INFO] Creating standalone zip: %ZIP_OUT%
powershell -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%ZIP_OUT%' -Force"

echo.
echo ======================================================================
echo  RELEASE BUILD COMPLETED SUCCESSFULLY!
echo  Release folder : %DIST_DIR%
echo  Release archive: %ZIP_OUT%
echo ======================================================================
endlocal
