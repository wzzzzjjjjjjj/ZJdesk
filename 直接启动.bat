@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-both.ps1" -SkipBuild
if errorlevel 1 (
  echo.
  echo Start failed. See the messages above.
  pause
  exit /b 1
)
