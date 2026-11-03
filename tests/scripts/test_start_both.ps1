$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ((Get-Content -Raw -LiteralPath (Join-Path $projectRoot '构建启动.bat')) -notmatch '-ForceRestart') {
    throw 'The build launcher must request a force restart.'
}
if ((Get-Content -Raw -LiteralPath (Join-Path $projectRoot '直接启动.bat')) -match '-ForceRestart') {
    throw 'Direct startup must retain its existing behavior.'
}
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
param($Preset, $ServerUrl, [scriptblock]$OnClientStarted)
$heldUntilStart = $false
try {
    $probe = [IO.File]::Open((Join-Path $PSScriptRoot 'build\start-both\launcher.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $probe.Dispose()
} catch [IO.IOException] { $heldUntilStart = $true }
if (-not $heldUntilStart) { throw 'Launcher lock was released before Windows client creation.' }
if ($Scenario -eq 'RestartClientFail') { throw 'Windows client creation failed in fixture.' }
if (-not $OnClientStarted) { throw 'Missing client-start callback.' }
& $OnClientStarted
$probe = [IO.File]::Open((Join-Path $PSScriptRoot 'build\start-both\launcher.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
$probe.Dispose()
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
function Get-Process {
    param($Name, $ErrorAction)
    if (($Name -join ',') -ne 'zjdesk,zjdesk-server') { throw 'Unexpected process selection.' }
    if ($Scenario -in @('RestartBusy', 'RestartWindowsFail', 'RestartWaitFail', 'RestartLegacyLock')) {
        foreach ($index in 1, 2) {
            $process = [pscustomobject]@{ Id = 100 + $index; ProcessName = $Name[$index - 1]; HasExited = $false }
            $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {
                param($Timeout)
                if ($Timeout -ne 8000) { throw 'Unexpected exit timeout.' }
                return $this.HasExited
            }
            $process
        }
    }
}
function Stop-Process {
    param($InputObject, [switch]$Force, $ErrorAction)
    if (-not $Force) { throw 'Instance shutdown must be forced.' }
    Write-Host "WINDOWS_STOPPED=$($InputObject.Id)"
    if ($Scenario -eq 'RestartWindowsFail') { throw 'Access denied in fixture.' }
    if ($Scenario -ne 'RestartWaitFail') { $InputObject.HasExited = $true }
    if ($Scenario -eq 'RestartLegacyLock') { $global:LauncherTestLockRelease.Set() | Out-Null }
}
function ssh {
    $global:LASTEXITCODE = 0
    Write-Host 'SSH_CALLED'
    $command = $args[-1]
    if ($command -like 'test -d *' -and $Scenario -eq 'RestartPreflightFail') { $global:LASTEXITCODE = 1 }
    if ($command -like '*pkill -KILL*') {
        if (-not $command.Contains("pkill -KILL -u `$uid -x 'zjdesk|zjdesk-server'")) { throw 'Linux shutdown must select exact application names and the SSH user.' }
        if ($command -like '*pkill -f*') { throw 'Shutdown must not match arbitrary command lines.' }
        Write-Host 'LINUX_STOPPED'
        if ($Scenario -eq 'RestartLinuxFail') { $global:LASTEXITCODE = 1 }
    }
    if ($command -like 'zjdesk_listeners=*') {
        if ($Scenario -in @('PortBusy', 'RestartPortBusy')) { $global:LASTEXITCODE = 20 }
        if ($Scenario -eq 'RestartPortProbeFail') { $global:LASTEXITCODE = 22 }
    }
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
function Start-Sleep {
    if ($Scenario -eq 'RestartLegacyLock') { [Threading.Thread]::Sleep(20) }
}
if ($Scenario -eq 'RestartLegacyLock') {
    $global:LauncherTestLockRelease = [Threading.ManualResetEvent]::new($false)
    $lockReady = [Threading.ManualResetEvent]::new($false)
    $holder = [powershell]::Create()
    $null = $holder.AddScript({
        param($Path, $Ready, $Release)
        $lock = [IO.File]::Open($Path, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { $Ready.Set() | Out-Null; $Release.WaitOne(10000) | Out-Null }
        finally { $lock.Dispose() }
    }).AddArgument((Join-Path $PSScriptRoot 'build\start-both\launcher.lock')).AddArgument($lockReady).AddArgument($global:LauncherTestLockRelease)
    $holderResult = $holder.BeginInvoke()
    if (-not $lockReady.WaitOne(5000)) { throw 'Legacy lock fixture did not start.' }
}
if ($Scenario -eq 'RestartActiveBuild') {
    $heldBuildLock = [IO.File]::Open((Join-Path $PSScriptRoot 'build\start-both\launcher.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}
$parameters = @{ SkipBuild = $true }
if ($Scenario -like 'Restart*') { $parameters.ForceRestart = $true }
if ($Scenario -in @('BuildSuccess', 'LinuxFail', 'WindowsFail', 'RestartBuild')) { $parameters.SkipBuild = $false }
if ($Scenario -eq 'Explicit') { $parameters.WindowsAddress = '10.11.42.99' }
if ($Scenario -eq 'StaleExplicit') { $parameters.WindowsAddress = '10.11.42.3' }
& (Join-Path $PSScriptRoot 'start-both.ps1') @parameters
if ($Scenario -eq 'RestartLegacyLock') {
    $holder.EndInvoke($holderResult)
    $holder.Dispose()
    $global:LauncherTestLockRelease.Dispose()
    $lockReady.Dispose()
}
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
    @{ Name = 'WindowsFail'; ExitCode = 1; Expected = 'Windows build or deployment failed.' },
    @{ Name = 'RestartIdle'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'RestartBusy'; ExitCode = 0; Expected = 'Stopped Windows zjdesk-server (PID 102).' },
    @{ Name = 'RestartWindowsFail'; ExitCode = 1; Expected = 'Could not force-stop Windows ZJdesk PID 101' },
    @{ Name = 'RestartWaitFail'; ExitCode = 1; Expected = 'Timed out waiting for process exit.' },
    @{ Name = 'RestartLinuxFail'; ExitCode = 1; Expected = 'Could not force-stop Linux ZJdesk instances.' },
    @{ Name = 'RestartPortBusy'; ExitCode = 1; Expected = 'Linux port 8765 is still in use after stopping ZJdesk.' },
    @{ Name = 'RestartPreflightFail'; ExitCode = 1; Expected = 'Linux SSH, desktop, project directory or media address is unavailable.' },
    @{ Name = 'RestartBuild'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'RestartLegacyLock'; ExitCode = 0; Expected = 'Stopping the previous Windows instance and waiting for the launcher lock...' },
    @{ Name = 'RestartActiveBuild'; ExitCode = 1; Expected = 'Another build or launch is already running.' },
    @{ Name = 'RestartPortProbeFail'; ExitCode = 1; Expected = 'Could not check Linux signaling port 8765.' },
    @{ Name = 'RestartClientFail'; ExitCode = 1; Expected = 'Windows client creation failed in fixture.' }
)
$failures = 0
foreach ($case in $cases) {
    # Each scenario starts independently of the failed-build marker left by earlier scenarios.
    Remove-Item -LiteralPath (Join-Path $testRoot 'build\start-both\build-incomplete') -Force -ErrorAction SilentlyContinue
    $output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testRoot 'driver.ps1') $case.Name 2>&1) -join "`n"
    $exitCode = $LASTEXITCODE
    $unexpectedSsh = $case.BeforeSsh -and $output.Contains('SSH_CALLED')
    $invalidBuildFlow = ($case.Name -in @('BuildSuccess', 'RestartBuild') -and (-not $output.Contains('LINUX_BUILD_OK') -or -not $output.Contains('WINDOWS_BUILD_OK'))) -or
        ($case.Name -in @('LinuxFail', 'WindowsFail') -and $output.Contains('LAUNCH_ADDRESS='))
    $invalidRestart = ($case.Name -like 'Restart*' -and $case.ExitCode -eq 1 -and $output.Contains('LAUNCH_ADDRESS=')) -or
        ($case.Name -in @('RestartWindowsFail', 'RestartWaitFail', 'RestartPreflightFail', 'RestartActiveBuild') -and $output.Contains('LINUX_STOPPED')) -or
        ($case.Name -eq 'RestartPreflightFail' -and $output.Contains('WINDOWS_STOPPED=')) -or
        ($case.Name -eq 'RestartBusy' -and (-not $output.Contains('WINDOWS_STOPPED=101') -or -not $output.Contains('LINUX_STOPPED') -or -not $output.Contains('LAUNCH_ADDRESS='))) -or
        ($case.Name -eq 'RestartLegacyLock' -and (-not $output.Contains('WINDOWS_STOPPED=101') -or -not $output.Contains('LAUNCH_ADDRESS='))) -or
        ($case.Name -eq 'RestartBuild' -and ($output.IndexOf('LINUX_STOPPED') -lt 0 -or $output.IndexOf('LINUX_STOPPED') -gt $output.IndexOf('SNAPSHOT_VERIFY=False')))
    if ($exitCode -ne $case.ExitCode -or -not $output.Contains($case.Expected) -or $unexpectedSsh -or $invalidBuildFlow -or $invalidRestart) {
        Write-Host "FAIL $($case.Name): exit $exitCode (expected $($case.ExitCode))`n$output" -ForegroundColor Red
        $failures++
    } else {
        Write-Host "PASS $($case.Name)"
    }
}
if ($failures) { throw "$failures launcher regression(s) failed. Fixtures: $testRoot" }
Write-Host "All $($cases.Count) launcher regressions passed. Fixtures: $testRoot"
