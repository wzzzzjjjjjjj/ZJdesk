param(
    [string]$QtRoot = 'F:\Qt6\6.8.3\msvc2022_64',
    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-debug'
)
$ErrorActionPreference = 'Stop'
$binaryRoot = Join-Path (Split-Path $PSScriptRoot -Parent) "build\$Preset\bin"
foreach ($binaryName in @('zjdesk-server.exe', 'zjdesk.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot $binaryName))) { throw 'Build the project first.' }
}
function Start-DemoProcess([string]$Executable, [string[]]$Arguments, [bool]$Background) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Join-Path $binaryRoot $Executable
    $startInfo.WorkingDirectory = $binaryRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $Background
    $startInfo.EnvironmentVariables['PATH'] = "$QtRoot\bin;" + $env:PATH
    $startInfo.EnvironmentVariables['QT_PLUGIN_PATH'] = "$QtRoot\plugins"
    $startInfo.Arguments = $Arguments -join ' '
    [System.Diagnostics.Process]::Start($startInfo)
}
$demoServer = Start-DemoProcess 'zjdesk-server.exe' @('--port', '8765') $true
try {
    Start-Sleep -Milliseconds 400
    if ($demoServer.HasExited) { throw 'Demo server could not start; check whether port 8765 is already in use.' }
    $demoAlice = Start-DemoProcess 'zjdesk.exe' @('--name', 'Alice', '--room', 'demo') $false
    $demoBob = Start-DemoProcess 'zjdesk.exe' @('--name', 'Bob', '--room', 'demo') $false
    Write-Output "Server PID: $($demoServer.Id); Alice PID: $($demoAlice.Id); Bob PID: $($demoBob.Id)"
    Write-Output 'Click Join in both clients. Close both windows to stop the demo server.'
    $demoAlice.WaitForExit()
    $demoBob.WaitForExit()
} finally {
    if (-not $demoServer.HasExited) { $demoServer.Kill(); $demoServer.WaitForExit() }
}
