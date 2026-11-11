param(
    [string[]]$LinuxSsh = @(),
    [string]$LinuxAddress = '',
    [string]$LinuxRoot = '/home/orbbec/ZJdesk/zjdesk',
    [string]$WindowsAddress = '',
    [string]$Display = ':1',
    [switch]$SkipBuild,
    [switch]$ForceRestart,
    [switch]$PreferTailscale
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
. (Join-Path $PSScriptRoot 'select-network.ps1')
$logDir = Join-Path $PSScriptRoot 'build\start-both'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$tempDir = Join-Path $logDir 'temp'
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
$env:TEMP = $tempDir
$env:TMP = $tempDir
$originalLinuxRoot = $LinuxRoot
$remoteMarker = "$originalLinuxRoot/build/start-both-current"
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
try {
    $sourceKey = -join ($hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes(([Environment]::MachineName + '|' + $PSScriptRoot).ToLowerInvariant())) | ForEach-Object { $_.ToString('x2') })
} finally { $hashAlgorithm.Dispose() }
$incrementalLinuxRoot = "$originalLinuxRoot/build/start-both-cache.$($sourceKey.Substring(0, 16))"
$pendingBuild = Join-Path $logDir 'build-incomplete'
$lockStream = $null
function Stop-WindowsZjdeskInstances {
    foreach ($process in @(Get-Process -Name 'zjdesk', 'zjdesk-server' -ErrorAction SilentlyContinue)) {
        try {
            if (-not $process.HasExited) {
                Stop-Process -InputObject $process -Force -ErrorAction Stop
                if (-not $process.WaitForExit(8000)) { throw 'Timed out waiting for process exit.' }
                Write-Host "Stopped Windows $($process.ProcessName) (PID $($process.Id))."
            }
        } catch {
            if (-not $process.HasExited) { throw "Could not force-stop Windows ZJdesk PID $($process.Id): $($_.Exception.Message)" }
        }
    }
}
try {
    if (-not $LinuxSsh.Count) {
        Write-Host 'Linux IPv4: run sh tools/ip-info.sh on Linux. Windows IPv4: run tools\ip-info.cmd on Windows.'
        $endpoint = Read-Host 'Enter Linux SSH target (your Linux username@IPv4 address)'
        if (-not $endpoint.Trim()) { throw 'Linux SSH target is required. Alternatively pass -LinuxSsh username@IPv4.' }
        $LinuxSsh = @($endpoint.Trim())
    }
    Write-Host 'Checking Linux SSH and display...'
    $network = Resolve-ZjdeskNetwork -LinuxSsh $LinuxSsh -LinuxAddress $LinuxAddress -WindowsAddress $WindowsAddress -PreferTailscale:$PreferTailscale
    $LinuxSsh = $network.LinuxSsh
    $LinuxAddress = $network.LinuxAddress
    $WindowsAddress = $network.WindowsAddress
    Write-Host "Media addresses: Windows $WindowsAddress ($($network.WindowsInterface), $($network.WindowsKind)); Linux $LinuxAddress ($($network.Interface), $($network.Kind)); SSH $LinuxSsh"
    & ssh -n -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh "test -d '$LinuxRoot' && test -r /run/user/1000/gdm/Xauthority && ip -4 -o addr show | grep -Fq ' $LinuxAddress/'"
    if ($LASTEXITCODE -ne 0) { throw 'Linux SSH, desktop, project directory or media address is unavailable.' }
    try {
        $lockStream = [IO.File]::Open((Join-Path $logDir 'launcher.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        if (-not $ForceRestart) { throw 'Another build or launch is already running.' }
        # run.ps1 keeps the old launcher alive until its Windows client exits.
        Write-Host 'Stopping the previous Windows instance and waiting for the launcher lock...'
        Stop-WindowsZjdeskInstances
        foreach ($attempt in 1..25) {
            Start-Sleep -Milliseconds 200
            try {
                $lockStream = [IO.File]::Open((Join-Path $logDir 'launcher.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
                break
            } catch { }
        }
        if ($null -eq $lockStream) { throw 'Another build or launch is already running.' }
    }
    if ($ForceRestart) {
        Write-Host 'Force-stopping existing Windows and Linux ZJdesk clients and signaling servers...'
        Stop-WindowsZjdeskInstances
        $stopLinuxCommand = @'
set -eu
uid=$(id -u)
if pkill -KILL -u $uid -x 'zjdesk|zjdesk-server'; then
    printf 'Stopped Linux ZJdesk clients and signaling servers.\n'
else
    zjdesk_stop_result=$?
    if [ $zjdesk_stop_result -ne 1 ]; then exit $zjdesk_stop_result; fi
fi
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if pgrep -u $uid -x -r D,I,R,S,T,t,W,X 'zjdesk|zjdesk-server' >/dev/null; then
        sleep 0.1
    else
        zjdesk_stop_result=$?
        if [ $zjdesk_stop_result -eq 1 ]; then exit 0; fi
        exit $zjdesk_stop_result
    fi
done
printf 'Linux ZJdesk instances did not exit.\n' >&2
exit 21
'@
        & ssh -n -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh ($stopLinuxCommand -replace "`r`n", "`n")
        if ($LASTEXITCODE -ne 0) { throw 'Could not force-stop Linux ZJdesk instances. Check SSH permissions and running processes.' }
    }
    $checkLinuxPortCommand = @'
zjdesk_listeners=$(ss -H -lnt 'sport = :8765') || exit 22
case $zjdesk_listeners in '') ;; *) exit 20;; esac
'@
    & ssh -n -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh ($checkLinuxPortCommand -replace "`r`n", "`n")
    if ($LASTEXITCODE -eq 20) {
        if ($ForceRestart) { throw 'Linux port 8765 is still in use after stopping ZJdesk. Check for another program using this port.' }
        throw 'Linux port 8765 is already in use. Close the existing Linux client/server and retry.'
    }
    if ($LASTEXITCODE -ne 0) { throw 'Could not check Linux signaling port 8765.' }
    if ($SkipBuild) {
        if (Test-Path -LiteralPath $pendingBuild) { throw 'The previous build did not complete on both machines. Run the build-and-start launcher again.' }
        Write-Host 'Using existing Release binaries; build both machines first after code changes.'
        $savedRoot = (& ssh -n -o BatchMode=yes $LinuxSsh "if test -f '$remoteMarker'; then cat '$remoteMarker'; else printf 'LEGACY'; fi")
        if ($LASTEXITCODE -ne 0 -or -not $savedRoot) { throw 'Could not read the Linux build record.' }
        if ($savedRoot -eq 'LEGACY') {
            Write-Host 'No synchronized build record; using the original Linux build.'
        } elseif ($savedRoot -is [string] -and ($savedRoot.Trim() -eq $incrementalLinuxRoot -or
            $savedRoot -match ('^' + [regex]::Escape("$originalLinuxRoot/build/start-both-source.") + '[A-Za-z0-9]{8}$'))) {
            $LinuxRoot = $savedRoot.Trim()
        } else {
            throw 'Invalid Linux build record. Run the build-and-start launcher again.'
        }
        foreach ($binary in @('zjdesk.exe', 'zjdesk-server.exe')) {
            if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "build\windows-release\bin\$binary") -PathType Leaf)) {
                throw "Windows Release binary missing: $binary. Run the build-and-start launcher first."
            }
        }
        & ssh -n -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh "test -x '$LinuxRoot/build/linux-release/bin/zjdesk' && test -x '$LinuxRoot/build/linux-release/bin/zjdesk-server'"
        if ($LASTEXITCODE -ne 0) { throw 'Linux Release binaries are missing or not executable. Run the build-and-start launcher first.' }
    } else {
        New-Item -ItemType File -Force -Path $pendingBuild | Out-Null
        Write-Host 'Synchronizing source into the reusable Linux build directory...'
        $archive = Join-Path $tempDir 'source-snapshot.zip'
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive
        $LinuxRoot = $incrementalLinuxRoot
        & ssh -n -o BatchMode=yes $LinuxSsh "mkdir -p '$originalLinuxRoot/build'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not prepare the Linux build directory.' }
        & scp -q $archive "${LinuxSsh}:${LinuxRoot}.zip"
        if ($LASTEXITCODE -ne 0) { throw 'Could not transfer the source snapshot to Linux.' }
        & scp -q (Join-Path $PSScriptRoot 'sync-source.sh') "${LinuxSsh}:${LinuxRoot}.sync.sh"
        if ($LASTEXITCODE -ne 0) { throw 'Could not transfer the source synchronizer to Linux.' }
        & ssh -n -o BatchMode=yes $LinuxSsh "bash '$LinuxRoot.sync.sh' '$LinuxRoot.zip' '$LinuxRoot' && rm -- '$LinuxRoot.sync.sh' '$LinuxRoot.zip'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not synchronize the source snapshot on Linux.' }
        Write-Host 'Building Linux Release incrementally (tests skipped)...'
        & ssh -n -o BatchMode=yes $LinuxSsh "cd '$LinuxRoot' && bash ./build.sh --preset linux-release --deploy --skip-tests" 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'linux-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Linux build or deployment failed.' }
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive -Verify
        Write-Host 'Building Windows Release incrementally (tests skipped)...'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') -Preset windows-release -Deploy -SkipTests 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'windows-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Windows build or deployment failed.' }
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive -Verify
        & ssh -n -o BatchMode=yes $LinuxSsh "printf '%s\n' '$LinuxRoot' > '$remoteMarker'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not record the Linux build for direct startup.' }
        Remove-Item -LiteralPath $pendingBuild
    }
    Write-Host 'Launching Linux client and signaling server...'
    $remoteCommand = "cd '$LinuxRoot' && nohup env -u ZJDESK_WEBRTC_MAX_BITRATE -u ZJDESK_LIBNICE_ROOT DISPLAY='$Display' XAUTHORITY=/run/user/1000/gdm/Xauthority ZJDESK_WEBRTC_LOCAL_ADDRESS='$LinuxAddress' ZJDESK_WEBRTC_GCC=1 bash ./run.sh > build/start-both.log 2>&1 < /dev/null &"
    & ssh -n -o BatchMode=yes $LinuxSsh $remoteCommand
    if ($LASTEXITCODE -ne 0) { throw 'Linux launch command failed.' }
    Start-Sleep -Seconds 3
    & ssh -n -o BatchMode=yes $LinuxSsh "ss -lnt | grep -q ':8765 ' && pgrep -f '[b]uild/linux-release/bin/zjdesk --server ' >/dev/null"
    if ($LASTEXITCODE -ne 0) { throw 'Linux client or server did not start; inspect build/start-both.log on Linux.' }
    Write-Host "Launching Windows client. Server: ws://${LinuxAddress}:8765; enter the same room code on both clients"
    $env:ZJDESK_WEBRTC_LOCAL_ADDRESS = $WindowsAddress
    $env:ZJDESK_WEBRTC_GCC = '1'
    Remove-Item Env:ZJDESK_WEBRTC_MAX_BITRATE -ErrorAction SilentlyContinue
    # Keep the mutex through client creation, then release it before run.ps1 waits.
    $onClientStarted = { $lockStream.Dispose() }.GetNewClosure()
    & (Join-Path $PSScriptRoot 'run.ps1') -Preset windows-release -ServerUrl "ws://${LinuxAddress}:8765" -OnClientStarted $onClientStarted
} catch {
    Write-Host $_ -ForegroundColor Red
    if ($env:ZJDESK_LAUNCH_DEBUG) {
        Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor Yellow
        Write-Host $_.ScriptStackTrace -ForegroundColor Yellow
        Write-Host "lockStream is null: $($null -eq $lockStream); onClientStarted is null: $($null -eq $onClientStarted)" -ForegroundColor Yellow
    }
    exit 1
} finally {
    if ($null -ne $lockStream) { $lockStream.Dispose() }
}
