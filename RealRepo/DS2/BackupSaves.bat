@ECHO OFF

SET pshell=powershell
WHERE /Q %pshell%
IF NOT ERRORLEVEL 1 GOTO havepshell
SET pshell=%SYSTEMROOT%\System32\WindowsPowerShell\v1.0\powershell
IF EXIST %pshell%.exe GOTO havepshell
ECHO ERROR: PowerShell is needed for this batch file, but it cannot be detected
ECHO        on your system (it should be preinstalled on Windows 10 since version
ECHO        1607). You should either check your Windows installation or go to this
ECHO        Microsoft page and install PowerShell yourself:
ECHO        https://tinyurl.com/y85wsdr4
ECHO(
GOTO abort

:havepshell
FOR /F "tokens=* USEBACKQ" %%F IN (`%pshell% Get-Date -Format "{yyyy-MM-dd HH-mm-ss}"`) DO SET timestamp=%%F
SET srcpath="%APPDATA%\DarkSoulsII"
SET backuppath="%APPDATA%\DarkSoulsIIBackup\%timestamp%"

XCOPY %srcpath% %backuppath% /E /I /Q /Y
IF NOT ERRORLEVEL 1 GOTO finish
ECHO Error while copying the game save files from %srcpath% to %backuppath%

:abort
ECHO Aborting batch file
GOTO exitnow
:finish
ECHO All done!
:exitnow
