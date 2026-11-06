$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ((Get-Content -Raw -LiteralPath (Join-Path $projectRoot '构建启动.bat')) -notmatch '-ForceRestart') {
    throw 'The build launcher must request a force restart.'
}
$directLauncher = Get-Content -Raw -LiteralPath (Join-Path $projectRoot '直接启动.bat')
if ($directLauncher -match 'start-both|run\.ps1|Read-Host|-ForceRestart' -or
    -not $directLauncher.Contains('start "" /D "%~dp0dist\windows-release\bin" "%~dp0dist\windows-release\bin\zjdesk.exe"')) {
    throw 'Direct startup must open the deployed client without SSH, builds or IP prompts.'
}
$testRoot = Join-Path $projectRoot ('build\launcher-tests.' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $testRoot 'build\windows-release\bin') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'start-both.ps1') -Destination $testRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'select-network.ps1') -Destination $testRoot
# Replace only the real socket operation; selection and SSH fallback run unchanged.
@'
function Test-ZjdeskTcpPort {
    param($Address, $LocalAddress)
    if (-not $LocalAddress) { throw 'Reachability must use the chosen Windows source address.' }
    if ($Scenario -eq 'WindowsWiredBlocked' -and $LocalAddress -eq '10.11.42.57') { return $false }
    return $Scenario -ne 'NoReachable' -and -not ($Scenario -eq 'WiredBlocked' -and $Address -eq '10.11.42.4')
}
'@ | Add-Content -Encoding UTF8 -LiteralPath (Join-Path $testRoot 'select-network.ps1')
Copy-Item -LiteralPath (Join-Path $projectRoot 'sync-source.sh') -Destination $testRoot
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
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $key = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(([Environment]::MachineName + '|' + $PSScriptRoot).ToLowerInvariant())) | ForEach-Object { $_.ToString('x2') })
} finally { $sha.Dispose() }
$expectedCacheRoot = '/home/orbbec/ZJdesk/zjdesk/build/start-both-cache.' + $key.Substring(0, 16)
function Find-NetRoute {
    param($RemoteIPAddress, $LocalIPAddress, $InterfaceIndex, $ErrorAction)
    if ($RemoteIPAddress -notin @('10.11.42.4', '10.11.47.137', '10.11.42.8')) { throw 'Route lookup used the wrong Linux address.' }
    [pscustomobject]@{ DestinationPrefix = '10.11.42.0/24'; NextHop = '0.0.0.0' }
    if ($Scenario -ne 'NoRoute' -and -not ($Scenario -eq 'WindowsWiredNoRoute' -and $LocalIPAddress -eq '10.11.42.57')) {
        $address = '10.11.42.57'
        if ($RemoteIPAddress -eq '10.11.47.137') { $address = '10.11.48.95' }
        if ($Scenario -eq 'DhcpChanged') { $address = '10.11.42.58' }
        if ($Scenario -eq 'WindowsDefaultRouteWifi') { $address = '10.11.48.95' }
        if ($Scenario -eq 'WindowsVirtualOnly') { $address = '100.100.100.100' }
        if ($LocalIPAddress) { $address = $LocalIPAddress }
        [pscustomobject]@{ IPAddress = $address }
    }
}
function Get-NetIPAddress {
    param($AddressFamily, $ErrorAction)
    [pscustomobject]@{ IPAddress = '100.100.100.100'; InterfaceIndex = 30; AddressState = 'Preferred'; SkipAsSource = $false }
    $address = if ($Scenario -eq 'DhcpChanged') { '10.11.42.58' } else { '10.11.42.57' }
    if ($Scenario -notin @('WindowsWirelessOnly', 'BothWirelessOnly')) {
        [pscustomobject]@{ IPAddress = $address; InterfaceIndex = 15; AddressState = 'Preferred'; SkipAsSource = $false }
    }
    [pscustomobject]@{ IPAddress = '10.11.48.95'; InterfaceIndex = 22; AddressState = 'Preferred'; SkipAsSource = $false }
    [pscustomobject]@{ IPAddress = '10.11.42.99'; InterfaceIndex = 15; AddressState = 'Preferred'; SkipAsSource = $true }
}
function Get-NetAdapter {
    param([switch]$Physical, $ErrorAction)
    if (-not $Physical) { throw 'Automatic selection must exclude virtual Windows adapters.' }
    if ($Scenario -eq 'WindowsVirtualOnly') { return }
    # Wi-Fi first, even when the system default route prefers it.
    [pscustomobject]@{ Name = 'WLAN'; ifIndex = 22; Status = 'Up'; NdisPhysicalMedium = 9 }
    if ($Scenario -notin @('WindowsWirelessOnly', 'BothWirelessOnly')) {
        $status = if ($Scenario -eq 'WindowsDisconnected') { 'Disconnected' } else { 'Up' }
        $medium = if ($Scenario -eq 'WindowsUnspecifiedEthernet') { 0 } else { 14 }
        [pscustomobject]@{ Name = 'Ethernet'; ifIndex = 15; Status = $status; NdisPhysicalMedium = $medium; MediaType = '802.3' }
    }
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
    if ($command -like 'ip -4 -o addr*') {
        if ($Scenario -eq 'SshUnavailable' -or ($Scenario -in @('WirelessOnly', 'BothWirelessOnly') -and $args[-2] -eq 'orbbec@10.11.42.4')) {
            $global:LASTEXITCODE = 255
            return
        }
        # Deliberately list Wi-Fi first in the preference test.
        if ($Scenario -in @('WirelessOnly', 'BothWirelessOnly', 'WiredBlocked', 'WiredPreferred', 'ExplicitWireless', 'StaleLinux')) {
            "wireless`twlp0s20f3`t10.11.47.137"
        }
        if ($Scenario -eq 'VirtualOnly') { "other`ttailscale0`t100.87.60.58" }
        if ($Scenario -notin @('WirelessOnly', 'BothWirelessOnly', 'VirtualOnly')) {
            $address = if ($Scenario -eq 'LinuxDhcpChanged') { '10.11.42.8' } else { '10.11.42.4' }
            "wired`tenp0s31f6`t$address"
        }
        return
    }
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
    if ($command -like '*mktemp -d*') { throw 'The launcher must reuse its build directory.' }
    if ($command -like 'bash *.sync.sh*') {
        if (-not $command.Contains("${expectedCacheRoot}.sync.sh")) { throw 'Cache directory is not stable for this workspace.' }
        Write-Host 'SOURCE_SYNC_OK'
        if ($Scenario -eq 'SyncFail') { $global:LASTEXITCODE = 1 }
    }
    if ($args[-1] -like '*bash ./build.sh*') {
        if ($args[-1] -notlike '*--deploy --skip-tests*') { throw 'Linux build must deploy and skip tests.' }
        Write-Host 'LINUX_BUILD_OK'
        if ($Scenario -eq 'LinuxFail') { $global:LASTEXITCODE = 1 }
    }
    if ($args[-1] -like 'if test -f *') {
        if ($Scenario -eq 'CachedDirect') { $expectedCacheRoot }
        elseif ($Scenario -eq 'OtherCacheRecord') { '/home/orbbec/ZJdesk/zjdesk/build/start-both-cache.0000000000000000' }
        else { 'LEGACY' }
    }
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
$parameters = @{ SkipBuild = $true; LinuxSsh = @('orbbec@10.11.42.4', 'orbbec@10.11.47.137') }
if ($Scenario -in @('PromptAddress', 'PromptEmpty')) {
    $parameters.Remove('LinuxSsh')
    function Read-Host {
        param($Prompt)
        Write-Host 'ADDRESS_PROMPTED'
        if ($Scenario -eq 'PromptAddress') { return 'orbbec@10.11.42.4' }
        return ''
    }
}
if ($Scenario -like 'Restart*') { $parameters.ForceRestart = $true }
if ($Scenario -in @('BuildSuccess', 'LinuxFail', 'WindowsFail', 'RestartBuild', 'SyncFail')) { $parameters.SkipBuild = $false }
if ($Scenario -eq 'Explicit') { $parameters.WindowsAddress = '10.11.42.99' }
if ($Scenario -eq 'StaleExplicit') { $parameters.WindowsAddress = '10.11.42.3' }
if ($Scenario -eq 'ExplicitWireless') { $parameters.LinuxAddress = '10.11.47.137' }
if ($Scenario -eq 'WindowsExplicitWireless') { $parameters.WindowsAddress = '10.11.48.95' }
if ($Scenario -eq 'StaleLinux') { $parameters.LinuxAddress = '10.11.42.3' }
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
    @{ Name = 'PromptAddress'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'PromptEmpty'; ExitCode = 1; Expected = 'Linux SSH target is required.'; BeforeSsh = $true },
    @{ Name = 'Automatic'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'DhcpChanged'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.58;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'Explicit'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.99;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'StaleExplicit'; ExitCode = 1; Expected = 'Windows media address 10.11.42.3 is unavailable.'; BeforeSsh = $true },
    @{ Name = 'NoRoute'; ExitCode = 1; Expected = 'No reachable Linux media address with a Windows route.' },
    @{ Name = 'WiredPreferred'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WirelessOnly'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.47.137:8765' },
    @{ Name = 'WiredBlocked'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.47.137:8765' },
    @{ Name = 'LinuxDhcpChanged'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.8:8765' },
    @{ Name = 'ExplicitWireless'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.47.137:8765' },
    @{ Name = 'WindowsDefaultRouteWifi'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsWirelessOnly'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsWiredBlocked'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsVirtualOnly'; ExitCode = 1; Expected = 'No reachable Linux media address with a Windows route.' },
    @{ Name = 'WindowsWiredNoRoute'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsDisconnected'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsUnspecifiedEthernet'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'WindowsExplicitWireless'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'BothWirelessOnly'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.48.95;SERVER=ws://10.11.47.137:8765' },
    @{ Name = 'StaleLinux'; ExitCode = 1; Expected = 'Linux media address 10.11.42.3 is unavailable.' },
    @{ Name = 'NoReachable'; ExitCode = 1; Expected = 'No reachable Linux media address with a Windows route.' },
    @{ Name = 'SshUnavailable'; ExitCode = 1; Expected = 'Could not query Linux network addresses over SSH.' },
    @{ Name = 'VirtualOnly'; ExitCode = 1; Expected = 'No reachable Linux media address with a Windows route.' },
    @{ Name = 'PortBusy'; ExitCode = 1; Expected = 'Linux port 8765 is already in use. Close the existing Linux client/server and retry.' },
    @{ Name = 'BuildSuccess'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'SyncFail'; ExitCode = 1; Expected = 'Could not synchronize the source snapshot on Linux.' },
    @{ Name = 'CachedDirect'; ExitCode = 0; Expected = 'LAUNCH_ADDRESS=10.11.42.57;SERVER=ws://10.11.42.4:8765' },
    @{ Name = 'OtherCacheRecord'; ExitCode = 1; Expected = 'Invalid Linux build record.' },
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
    $invalidBuildFlow = ($case.Name -in @('BuildSuccess', 'RestartBuild') -and (-not $output.Contains('SOURCE_SYNC_OK') -or -not $output.Contains('LINUX_BUILD_OK') -or -not $output.Contains('WINDOWS_BUILD_OK'))) -or
        ($case.Name -in @('LinuxFail', 'WindowsFail', 'SyncFail') -and $output.Contains('LAUNCH_ADDRESS='))
    if ($case.Name -in @('LinuxFail', 'WindowsFail', 'SyncFail') -and
        -not (Test-Path -LiteralPath (Join-Path $testRoot 'build/start-both/build-incomplete'))) { $invalidBuildFlow = $true }
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
