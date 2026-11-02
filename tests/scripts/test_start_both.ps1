$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testRoot = Join-Path $projectRoot ('build\launcher-tests.' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $testRoot 'build\windows-release\bin') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'start-both.ps1') -Destination $testRoot
foreach ($binary in @('zjdesk.exe', 'zjdesk-server.exe')) {
    New-Item -ItemType File -Path (Join-Path $testRoot "build\windows-release\bin\$binary") | Out-Null
}
@'
param($Preset, $ServerUrl)
Write-Output "LAUNCH_ADDRESS=$env:ZJDESK_WEBRTC_LOCAL_ADDRESS;SERVER=$ServerUrl"
'@ | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $testRoot 'run.ps1')
@'
param($Scenario)
$ErrorActionPreference = 'Stop'
function Find-NetRoute {
    param($RemoteIPAddress, $ErrorAction)
    if ($RemoteIPAddress -ne '10.11.42.4') { throw 'Route lookup used the wrong Linux address.' }
    [pscustomobject]@{ DestinationPrefix = '10.11.42.0/24'; NextHop = '0.0.0.0' }
    if ($Scenario -ne 'NoRoute') {
        $address = '10.11.42.57'
        if ($Scenario -eq 'DhcpChanged') { $address = '10.11.42.58' }
        [pscustomobject]@{ IPAddress = $address }
    }
}
function Get-NetIPAddress {
    param($AddressFamily, $ErrorAction)
    [pscustomobject]@{ IPAddress = '100.100.100.100' }
    [pscustomobject]@{ IPAddress = '10.11.42.57' }
    [pscustomobject]@{ IPAddress = '10.11.42.58' }
    [pscustomobject]@{ IPAddress = '10.11.42.99' }
}
function ssh {
    $global:LASTEXITCODE = 0
    if ($Scenario -eq 'PortBusy') { $global:LASTEXITCODE = 20 }
    Write-Host 'SSH_CALLED'
    if ($args[-1] -like 'if test -f *') { 'LEGACY' }
}
function Start-Sleep {}
$parameters = @{ SkipBuild = $true }
if ($Scenario -eq 'Explicit') { $parameters.WindowsAddress = '10.11.42.99' }
if ($Scenario -eq 'StaleExplicit') { $parameters.WindowsAddress = '10.11.42.3' }
& (Join-Path $PSScriptRoot 'start-both.ps1') @parameters
exit $LASTEXITCODE
'@ | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $testRoot 'driver.ps1')

$cases = @(
    @{ Name = 'Automatic'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'DhcpChanged'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.58;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'Explicit'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.99;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'StaleExplicit'; ExitCode = 1; Expected = 'Windows media address 10.11.42.3 is unavailable.'; BeforeSsh = $true },
    @{ Name = 'NoRoute'; ExitCode = 1; Expected = 'Could not select a Windows media address for Linux 10.11.42.4.'; BeforeSsh = $true },
    @{ Name = 'PortBusy'; ExitCode = 1; Expected = 'Linux port 8765 is already in use. Close the existing Linux client/server and retry.' }
)
$failures = 0
foreach ($case in $cases) {
    $output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testRoot 'driver.ps1') $case.Name 2>&1) -join "`n"
    $exitCode = $LASTEXITCODE
    $unexpectedSsh = $case.BeforeSsh -and $output.Contains('SSH_CALLED')
    if ($exitCode -ne $case.ExitCode -or -not $output.Contains($case.Expected) -or $unexpectedSsh) {
        Write-Host "FAIL $($case.Name): exit $exitCode (expected $($case.ExitCode))`n$output" -ForegroundColor Red
        $failures++
    } else {
        Write-Host "PASS $($case.Name)"
    }
}
if ($failures) { throw "$failures launcher regression(s) failed. Fixtures: $testRoot" }
Write-Host "All $($cases.Count) launcher regressions passed. Fixtures: $testRoot"
