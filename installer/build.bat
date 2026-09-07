@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo  Building DLSS 5 ^<^> VR Universal Installer (GUI)
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
echo [INFO] Compiling installer.cpp -> VR-DLSS5-Installer.exe...
cl /O2 /std:c++17 /EHsc /W3 /DUNICODE /D_UNICODE installer.cpp /link /SUBSYSTEM:WINDOWS /OUT:VR-DLSS5-Installer.exe user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib advapi32.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] VR-DLSS5-Installer.exe built successfully!
    del installer.obj >nul 2>&1
) else (
    echo.
    echo [FAILED] Compilation failed with error %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

endlocal
