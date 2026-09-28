<#
.SYNOPSIS
Configure, build and test ZJdesk from a regular PowerShell terminal.
.EXAMPLE
.\build.ps1
.EXAMPLE
.\build.ps1 -Preset windows-release -Deploy
.EXAMPLE
.\build.ps1 -GuiTests
#>
param(
    [string]$QtRoot = 'F:\Qt6\6.8.3\msvc2022_64',
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-debug',
    [switch]$Deploy,
    [switch]$Fresh,
    [switch]$GuiTests,
    [string]$GstRoot = '',
    [switch]$WithoutMedia
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2022 C++ tools were not found.' }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$cmakeDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDirectory = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($requiredPath in @("$QtRoot\bin\qmake.exe", "$cmakeDirectory\cmake.exe", "$ninjaDirectory\ninja.exe", $vcvars)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required tool is missing: $requiredPath" }
}
$buildRoot = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$commandPath = Join-Path $buildRoot "build-$Preset.cmd"
$testFilter = if ($GuiTests) { '' } else { '-E "^desktop$"' }
$mediaConfigure = ''
$mediaEnvironment = ''
$cachePath = Join-Path $projectRoot "build\$Preset\CMakeCache.txt"
if (-not $GstRoot -and -not $WithoutMedia -and (Test-Path -LiteralPath $cachePath)) {
    $gstCache = Select-String -LiteralPath $cachePath -Pattern '^GST_ROOT:PATH=(.+)$'
    $gstEnabled = Select-String -LiteralPath $cachePath -Pattern '^ZJDESK_ENABLE_GSTREAMER:BOOL=ON$'
    if ($gstCache -and $gstEnabled) { $GstRoot = $gstCache.Matches[0].Groups[1].Value }
}
if ($WithoutMedia) {
    $mediaConfigure = '-DZJDESK_ENABLE_GSTREAMER=OFF'
} elseif ($GstRoot) {
    $GstRoot = (Resolve-Path -LiteralPath $GstRoot).Path
    foreach ($gstFile in @('bin\gstreamer-1.0-0.dll', 'include\gstreamer-1.0\gst\gst.h', 'lib\gstapp-1.0.lib', 'libexec\gstreamer-1.0\gst-plugin-scanner.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $GstRoot $gstFile))) { throw "GStreamer SDK is incomplete: $gstFile" }
    }
    $mediaConfigure = "-DZJDESK_ENABLE_GSTREAMER=ON -DGST_ROOT=`"$GstRoot`""
    $mediaEnvironment = @"
set "PATH=$GstRoot\bin;%PATH%"
set "GST_PLUGIN_SYSTEM_PATH_1_0=$GstRoot\lib\gstreamer-1.0"
set "GST_PLUGIN_PATH_1_0="
set "GST_PLUGIN_SCANNER=$GstRoot\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
set "GST_REGISTRY_1_0=$buildRoot\gst-registry-$Preset.bin"
"@
}
$commands = @"
@echo off
chcp 65001 >nul
call "$vcvars"
if errorlevel 1 exit /b 1
set "QT_ROOT=$QtRoot"
set "PATH=$QtRoot\bin;$cmakeDirectory;$ninjaDirectory;%PATH%"
set "VSLANG=1033"
set "QT_PLUGIN_PATH=$QtRoot\plugins"
$mediaEnvironment
cd /d "$projectRoot"
cmake $(if ($Fresh) {'--fresh'}) --preset $Preset $mediaConfigure
if errorlevel 1 exit /b 1
cmake --build --preset $Preset --parallel 4
if errorlevel 1 exit /b 1
ctest --preset $Preset $testFilter
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
Write-Output "Build and selected tests passed: $projectRoot\build\$Preset\bin"
Write-Output "Run: & '$projectRoot\run.ps1' -Preset $Preset -QtRoot '$QtRoot'"
if ($Deploy) { Write-Output "Deployment: $deployRoot\bin" }
