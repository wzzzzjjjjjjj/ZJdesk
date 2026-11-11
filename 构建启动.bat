@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& .\start-both.ps1 -LinuxSsh @('orbbec@100.87.60.58', 'orbbec@10.11.42.4') -PreferTailscale -ForceRestart"
if errorlevel 1 (
  echo.
  echo Start failed. See the messages above.
  pause
  exit /b 1
)
