@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Preset windows-release -Deploy -SkipTests
if errorlevel 1 (
  echo.
  echo Build failed. See the messages above.
  pause
  exit /b 1
)
start "" /D "%~dp0dist\windows-release\bin" "%~dp0dist\windows-release\bin\zjdesk.exe"
if errorlevel 1 (
  echo.
  echo Start failed. See the messages above.
  pause
  exit /b 1
)
