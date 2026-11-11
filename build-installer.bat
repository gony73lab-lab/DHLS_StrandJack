@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Build Release x64 and create the versioned installer.
rem Optional PowerShell parameters are forwarded, e.g. -IsccPath "C:\Tools\ISCC.exe".
rem Set DHLS_INSTALLER_NO_PAUSE=1 when running from CI or another batch file.
set "taskScript=%~dp0DHLS_StrandJack\tools\Build-Installer.ps1"
set "taskShell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
rem Isolate Windows PowerShell modules from an inherited PowerShell 7 environment.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%ProgramFiles%\WindowsPowerShell\Modules"
set "taskExit=1"
if not exist "%taskScript%" goto missing_script
if not exist "%taskShell%" goto missing_shell
echo ============================================================
echo DHLS_StrandJack - Release x64 installer build
echo ============================================================
echo Version source: DHLS_StrandJack\AppVersion.h
echo Output folder:  %~dp0Dist

echo.
"%taskShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%taskScript%" %*
set "taskExit=%ERRORLEVEL%"
if not "%taskExit%"=="0" goto failed
echo.
echo SUCCESS: Installer and SHA-256 file created in Dist\VERSION.
goto finish
:missing_script
echo ERROR: Installer build script was not found:
echo "%taskScript%"
goto finish
:missing_shell
echo ERROR: Windows PowerShell 5.1 was not found.
goto finish
:failed
echo.
echo ERROR: Installer build failed. Exit code: %taskExit%
echo Check the error messages above. No application was installed or started.
:finish
echo.
if not defined DHLS_INSTALLER_NO_PAUSE pause
endlocal & exit /b %taskExit%
