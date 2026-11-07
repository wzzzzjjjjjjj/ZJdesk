@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-both.ps1" -LinuxSsh orbbec@100.87.60.58 -LinuxAddress 100.87.60.58 -WindowsAddress 100.122.3.99 -ForceRestart
if errorlevel 1 (
  echo.
  echo Start failed. See the messages above.
  pause
  exit /b 1
)
