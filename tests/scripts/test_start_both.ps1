$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testRoot = Join-Path $projectRoot ('build\launcher-tests.' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $testRoot 'build\windows-release\bin') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'start-both.ps1') -Destination $testRoot
@'
param($Archive, [switch]$Verify)
Write-Host "SNAPSHOT_VERIFY=$Verify"
'@ | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $testRoot 'snapshot-source.ps1')
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
    if ($args[-1] -like '*mktemp -d*') { '/home/orbbec/ZJdesk/zjdesk/build/start-both-source.Abc12345' }
    if ($args[-1] -like '*bash ./build.sh*') {
        if ($args[-1] -notlike '*--deploy --skip-tests*') { throw 'Linux build must deploy and skip tests.' }
        Write-Host 'LINUX_BUILD_OK'
        if ($Scenario -eq 'LinuxFail') { $global:LASTEXITCODE = 1 }
    }
    if ($args[-1] -like 'if test -f *') { 'LEGACY' }
}
function scp { $global:LASTEXITCODE = 0 }
function powershell.exe {
    if ($args -notcontains '-SkipTests' -or $args -notcontains '-Deploy') { throw 'Windows build must deploy and skip tests.' }
    Write-Host 'WINDOWS_BUILD_OK'
    $global:LASTEXITCODE = if ($Scenario -eq 'WindowsFail') { 1 } else { 0 }
}
function Start-Sleep {}
$parameters = @{ SkipBuild = $true }
if ($Scenario -in @('BuildSuccess', 'LinuxFail', 'WindowsFail')) { $parameters.SkipBuild = $false }
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
    @{ Name = 'PortBusy'; ExitCode = 1; Expected = 'Linux port 8765 is already in use. Close the existing Linux client/server and retry.' },
    @{ Name = 'BuildSuccess'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'LinuxFail'; ExitCode = 1; Expected = 'Linux build or deployment failed.' },
    @{ Name = 'WindowsFail'; ExitCode = 1; Expected = 'Windows build or deployment failed.' }
)
$failures = 0
foreach ($case in $cases) {
    $output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testRoot 'driver.ps1') $case.Name 2>&1) -join "`n"
    $exitCode = $LASTEXITCODE
    $unexpectedSsh = $case.BeforeSsh -and $output.Contains('SSH_CALLED')
    $invalidBuildFlow = ($case.Name -eq 'BuildSuccess' -and (-not $output.Contains('LINUX_BUILD_OK') -or -not $output.Contains('WINDOWS_BUILD_OK'))) -or
        ($case.Name -in @('LinuxFail', 'WindowsFail') -and $output.Contains('LAUNCH_ADDRESS='))
    if ($exitCode -ne $case.ExitCode -or -not $output.Contains($case.Expected) -or $unexpectedSsh -or $invalidBuildFlow) {
        Write-Host "FAIL $($case.Name): exit $exitCode (expected $($case.ExitCode))`n$output" -ForegroundColor Red
        $failures++
    } else {
        Write-Host "PASS $($case.Name)"
    }
}
if ($failures) { throw "$failures launcher regression(s) failed. Fixtures: $testRoot" }
Write-Host "All $($cases.Count) launcher regressions passed. Fixtures: $testRoot"
