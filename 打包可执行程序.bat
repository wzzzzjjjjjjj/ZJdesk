@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-release.ps1" %*
if errorlevel 1 goto :failed
echo.
pause
exit /b 0

:failed
echo.
echo Packaging failed. See the messages above.
pause
exit /b 1
