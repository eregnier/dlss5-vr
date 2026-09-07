@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo  Building DLSS 5 ^<^> VR Dual-Proxy (dxgi.dll)
echo ======================================================================

where cl >nul 2>&1
if %ERRORLEVEL% equ 0 goto :compile

set "VCVARS="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

if "%VCVARS%"=="" (
    echo [ERROR] Could not find vcvars64.bat! Please run from a Visual Studio x64 command prompt.
    exit /b 1
)

echo [INFO] Initializing MSVC x64 environment...
call "%VCVARS%" >nul 2>&1

:compile
echo [INFO] Compiling proxy.cpp -> dxgi.dll...
cl /O2 /LD /std:c++17 /EHsc proxy.cpp /link /DEF:proxy.def /OUT:dxgi.dll user32.lib gdi32.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] dxgi.dll built successfully!
    del proxy.obj dxgi.exp proxy.exp proxy.lib >nul 2>&1
) else (
    echo.
    echo [FAILED] Compilation failed with error %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

endlocal
