@echo off
cd /d "%~dp0"
if not exist "%~dp0dist\windows-release\bin\zjdesk.exe" (
  echo Windows client is missing. Build and deploy it first:
  echo powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Preset windows-release -Deploy
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
