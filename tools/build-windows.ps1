param(
    [string]$QtRoot = 'F:\Qt6\6.8.3\msvc2022_64',
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-debug',
    [switch]$Deploy,
    [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2022 C++ tools were not found.' }
$cmakeDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($requiredPath in @("$QtRoot\bin\qmake.exe", "$cmakeDirectory\cmake.exe", "$ninjaDirectory\ninja.exe", $vcvars)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required tool is missing: $requiredPath" }
}
$buildRoot = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$commandPath = Join-Path $buildRoot "build-$Preset.cmd"
$commands = @"
@echo off
chcp 65001 >nul
call "$vcvars"
if errorlevel 1 exit /b 1
set "QT_ROOT=$QtRoot"
set "PATH=$QtRoot\bin;$cmakeDirectory;$ninjaDirectory;%PATH%"
set "VSLANG=1033"
set "QT_PLUGIN_PATH=$QtRoot\plugins"
cd /d "$projectRoot"
cmake $(if ($Fresh) {'--fresh'}) --preset $Preset
if errorlevel 1 exit /b 1
cmake --build --preset $Preset --parallel 4
if errorlevel 1 exit /b 1
ctest --preset $Preset
if errorlevel 1 exit /b 1
"@
if ($Deploy) {
    $deployRoot = Join-Path $projectRoot "dist\$Preset"
    $commands += @"

cmake --install "build\$Preset" --prefix "$deployRoot"
if errorlevel 1 exit /b 1
"@
}
$commands += "`r`nexit /b 0`r`n"
[System.IO.File]::WriteAllText($commandPath, $commands, [System.Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $commandPath
if ($LASTEXITCODE -ne 0) { throw "Configure, build, test or deployment failed: $LASTEXITCODE" }
