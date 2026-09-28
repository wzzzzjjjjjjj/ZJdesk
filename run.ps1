<#
.SYNOPSIS
Run a local ZJdesk server and two client windows from PowerShell.
.EXAMPLE
.\run.ps1
.EXAMPLE
.\run.ps1 -Preset windows-release
#>
param(
    [string]$QtRoot = 'F:\Qt6\6.8.3\msvc2022_64',
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-debug',
    [string]$GstRoot = ''
)
$ErrorActionPreference = 'Stop'
$binaryRoot = Join-Path $PSScriptRoot "build\$Preset\bin"
foreach ($binaryName in @('zjdesk-server.exe', 'zjdesk.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot $binaryName))) {
        throw "Build the project first: .\build.ps1 -Preset $Preset"
    }
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$cachePath = Join-Path $PSScriptRoot "build\$Preset\CMakeCache.txt"
if (-not $GstRoot -and (Test-Path -LiteralPath $cachePath)) {
    $gstCache = Select-String -LiteralPath $cachePath -Pattern '^GST_ROOT:PATH=(.+)$'
    $gstEnabled = Select-String -LiteralPath $cachePath -Pattern '^ZJDESK_ENABLE_GSTREAMER:BOOL=ON$'
    if ($gstCache -and $gstEnabled) { $GstRoot = $gstCache.Matches[0].Groups[1].Value }
}
if ($GstRoot) { $GstRoot = (Resolve-Path -LiteralPath $GstRoot).Path }
foreach ($requiredPath in @("$QtRoot\bin\Qt6Core.dll", "$QtRoot\plugins\platforms\qwindows.dll")) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Qt runtime is missing: $requiredPath" }
}
function Start-DemoProcess([string]$Executable, [string[]]$Arguments, [bool]$Background) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Join-Path $binaryRoot $Executable
    $startInfo.WorkingDirectory = $binaryRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $Background
    $startInfo.EnvironmentVariables['PATH'] = "$QtRoot\bin;" + $env:PATH
    $startInfo.EnvironmentVariables['QT_PLUGIN_PATH'] = "$QtRoot\plugins"
    if ($GstRoot) {
        $startInfo.EnvironmentVariables['PATH'] = "$GstRoot\bin;" + $startInfo.EnvironmentVariables['PATH']
        $startInfo.EnvironmentVariables['GST_PLUGIN_SYSTEM_PATH_1_0'] = "$GstRoot\lib\gstreamer-1.0"
        $startInfo.EnvironmentVariables['GST_PLUGIN_PATH_1_0'] = ''
        $startInfo.EnvironmentVariables['GST_PLUGIN_SCANNER'] = "$GstRoot\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
        $startInfo.EnvironmentVariables['GST_REGISTRY_1_0'] = Join-Path $PSScriptRoot "build\gst-registry-$Preset.bin"
    }
    $startInfo.Arguments = $Arguments -join ' '
    [System.Diagnostics.Process]::Start($startInfo)
}
$demoServer = $null
$demoAlice = $null
$demoBob = $null
try {
    $demoServer = Start-DemoProcess 'zjdesk-server.exe' @('--port', '8765') $true
    Start-Sleep -Milliseconds 400
    if ($demoServer.HasExited) { throw 'Demo server could not start; check whether port 8765 is already in use.' }
    $demoAlice = Start-DemoProcess 'zjdesk.exe' @('--name', 'Alice', '--room', 'demo') $false
    $demoBob = Start-DemoProcess 'zjdesk.exe' @('--name', 'Bob', '--room', 'demo') $false
    Write-Output "Server PID: $($demoServer.Id); Alice PID: $($demoAlice.Id); Bob PID: $($demoBob.Id)"
    Write-Output 'Click Join in both clients. Close both windows, or press Ctrl+C here, to stop the demo.'
    while (-not $demoAlice.HasExited -or -not $demoBob.HasExited) {
        if ($demoServer.HasExited) { throw 'Demo server exited unexpectedly.' }
        Start-Sleep -Milliseconds 200
    }
} finally {
    foreach ($demoProcess in @($demoAlice, $demoBob, $demoServer)) {
        if ($null -ne $demoProcess) {
            if (-not $demoProcess.HasExited) { $demoProcess.Kill(); $demoProcess.WaitForExit() }
            $demoProcess.Dispose()
        }
    }
}
