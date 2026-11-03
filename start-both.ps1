param(
    [string]$LinuxSsh = 'orbbec@10.11.47.137',
    [string]$LinuxAddress = '10.11.42.4',
    [string]$LinuxRoot = '/home/orbbec/ZJdesk/zjdesk',
    [string]$WindowsAddress = '',
    [string]$Display = ':1',
    [switch]$SkipBuild,
    [switch]$ForceRestart
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$logDir = Join-Path $PSScriptRoot 'build\start-both'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$tempDir = Join-Path $logDir 'temp'
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
$env:TEMP = $tempDir
$env:TMP = $tempDir
$originalLinuxRoot = $LinuxRoot
$remoteMarker = "$originalLinuxRoot/build/start-both-current"
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
    Write-Host 'Checking Linux SSH and display...'
    if (-not $WindowsAddress) {
        $WindowsAddress = Find-NetRoute -RemoteIPAddress $LinuxAddress -ErrorAction Stop |
            Where-Object { $_.IPAddress } | Select-Object -First 1 -ExpandProperty IPAddress
        if (-not $WindowsAddress) { throw "Could not select a Windows media address for Linux $LinuxAddress. Pass -WindowsAddress explicitly." }
    }
    if (-not (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -eq $WindowsAddress })) { throw "Windows media address $WindowsAddress is unavailable." }
    Write-Host "Media addresses: Windows $WindowsAddress; Linux $LinuxAddress"
    & ssh -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh "test -d '$LinuxRoot' && test -r /run/user/1000/gdm/Xauthority && ip -4 -o addr show | grep -Fq ' $LinuxAddress/'"
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
        & ssh -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh ($stopLinuxCommand -replace "`r`n", "`n")
        if ($LASTEXITCODE -ne 0) { throw 'Could not force-stop Linux ZJdesk instances. Check SSH permissions and running processes.' }
    }
    $checkLinuxPortCommand = @'
zjdesk_listeners=$(ss -H -lnt 'sport = :8765') || exit 22
case $zjdesk_listeners in '') ;; *) exit 20;; esac
'@
    & ssh -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh ($checkLinuxPortCommand -replace "`r`n", "`n")
    if ($LASTEXITCODE -eq 20) {
        if ($ForceRestart) { throw 'Linux port 8765 is still in use after stopping ZJdesk. Check for another program using this port.' }
        throw 'Linux port 8765 is already in use. Close the existing Linux client/server and retry.'
    }
    if ($LASTEXITCODE -ne 0) { throw 'Could not check Linux signaling port 8765.' }
    if ($SkipBuild) {
        if (Test-Path -LiteralPath $pendingBuild) { throw 'The previous build did not complete on both machines. Run the build-and-start launcher again.' }
        Write-Host 'Using existing Release binaries; build both machines first after code changes.'
        $savedRoot = (& ssh -o BatchMode=yes $LinuxSsh "if test -f '$remoteMarker'; then cat '$remoteMarker'; else printf 'LEGACY'; fi")
        if ($LASTEXITCODE -ne 0 -or -not $savedRoot) { throw 'Could not read the Linux build record.' }
        if ($savedRoot -eq 'LEGACY') {
            Write-Host 'No synchronized build record; using the original Linux build.'
        } elseif ($savedRoot -is [string] -and $savedRoot -match ('^' + [regex]::Escape("$originalLinuxRoot/build/start-both-source.") + '[A-Za-z0-9]{8}$')) {
            $LinuxRoot = $savedRoot.Trim()
        } else {
            throw 'Invalid Linux build record. Run the build-and-start launcher again.'
        }
        foreach ($binary in @('zjdesk.exe', 'zjdesk-server.exe')) {
            if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "build\windows-release\bin\$binary") -PathType Leaf)) {
                throw "Windows Release binary missing: $binary. Run the build-and-start launcher first."
            }
        }
        & ssh -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh "test -x '$LinuxRoot/build/linux-release/bin/zjdesk' && test -x '$LinuxRoot/build/linux-release/bin/zjdesk-server'"
        if ($LASTEXITCODE -ne 0) { throw 'Linux Release binaries are missing or not executable. Run the build-and-start launcher first.' }
    } else {
        New-Item -ItemType File -Force -Path $pendingBuild | Out-Null
        Write-Host 'Copying the current Windows source to an isolated Linux build directory...'
        $archive = Join-Path $tempDir 'source-snapshot.zip'
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive
        $snapshotRoot = (& ssh -o BatchMode=yes $LinuxSsh "mkdir -p '$originalLinuxRoot/build' && mktemp -d '$originalLinuxRoot/build/start-both-source.XXXXXXXX'")
        if ($LASTEXITCODE -ne 0 -or -not $snapshotRoot -or -not $snapshotRoot.StartsWith("$originalLinuxRoot/build/start-both-source.")) { throw 'Could not create the Linux source directory.' }
        $LinuxRoot = $snapshotRoot.Trim()
        & scp -q $archive "${LinuxSsh}:${LinuxRoot}/source-snapshot.zip"
        if ($LASTEXITCODE -ne 0) { throw 'Could not transfer the source snapshot to Linux.' }
        & ssh -o BatchMode=yes $LinuxSsh "cd '$LinuxRoot' && python3 -m zipfile -e source-snapshot.zip . && rm source-snapshot.zip"
        if ($LASTEXITCODE -ne 0) { throw 'Could not unpack the source snapshot on Linux.' }
        Write-Host 'Building Linux Release (tests skipped)...'
        & ssh -o BatchMode=yes $LinuxSsh "cd '$LinuxRoot' && bash ./build.sh --preset linux-release --deploy --skip-tests" 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'linux-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Linux build or deployment failed.' }
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive -Verify
        Write-Host 'Building Windows Release (tests skipped)...'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') -Preset windows-release -Deploy -SkipTests 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'windows-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Windows build or deployment failed.' }
        & (Join-Path $PSScriptRoot 'snapshot-source.ps1') -Archive $archive -Verify
        & ssh -o BatchMode=yes $LinuxSsh "printf '%s\n' '$LinuxRoot' > '$remoteMarker'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not record the Linux build for direct startup.' }
        Remove-Item -LiteralPath $pendingBuild
    }
    Write-Host 'Launching Linux client and signaling server...'
    $remoteCommand = "cd '$LinuxRoot' && nohup env -u ZJDESK_WEBRTC_MAX_BITRATE -u ZJDESK_LIBNICE_ROOT DISPLAY='$Display' XAUTHORITY=/run/user/1000/gdm/Xauthority ZJDESK_WEBRTC_LOCAL_ADDRESS='$LinuxAddress' ZJDESK_WEBRTC_GCC=1 bash ./run.sh > build/start-both.log 2>&1 < /dev/null &"
    & ssh -o BatchMode=yes $LinuxSsh $remoteCommand
    if ($LASTEXITCODE -ne 0) { throw 'Linux launch command failed.' }
    Start-Sleep -Seconds 3
    & ssh -o BatchMode=yes $LinuxSsh "ss -lnt | grep -q ':8765 ' && pgrep -f '[b]uild/linux-release/bin/zjdesk --server ' >/dev/null"
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
    exit 1
} finally {
    if ($null -ne $lockStream) { $lockStream.Dispose() }
}
