@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1" -SkipBuild %*
set "packageExitCode=%ERRORLEVEL%"
if not "%packageExitCode%"=="0" (
    echo Packaging failed. Exit code: %packageExitCode%
    exit /b %packageExitCode%
)
echo Packages written to: "%~dp0dist\packages"
exit /b 0
