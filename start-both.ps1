param(
    [string]$LinuxSsh = 'orbbec@10.11.47.137',
    [string]$LinuxAddress = '10.11.42.4',
    [string]$LinuxRoot = '/home/orbbec/ZJdesk/zjdesk',
    [string]$WindowsAddress = '10.11.42.3',
    [string]$Display = ':1',
    [ValidateSet('demo')]
    [string]$Room = 'demo'
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$logDir = Join-Path $PSScriptRoot 'build\start-both'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$tempDir = Join-Path $logDir 'temp'
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
$env:TEMP = $tempDir
$env:TMP = $tempDir
try {
    Write-Host 'Checking Linux SSH and display...'
    if (-not (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -eq $WindowsAddress })) { throw "Windows media address $WindowsAddress is unavailable." }
    & ssh -o BatchMode=yes -o ConnectTimeout=8 $LinuxSsh "test -d '$LinuxRoot' && test -r /run/user/1000/gdm/Xauthority && ip -4 -o addr show | grep -Fq ' $LinuxAddress/' && ! ss -lnt | grep -q ':8765 '"
    if ($LASTEXITCODE -ne 0) { throw 'Linux SSH, desktop, project directory or port 8765 is unavailable.' }
    Write-Host 'Building Linux Release (including non-GUI tests)...'
    & ssh -o BatchMode=yes $LinuxSsh "cd '$LinuxRoot' && bash ./build.sh --preset linux-release --deploy" 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'linux-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Linux build or tests failed.' }
    Write-Host 'Building Windows Release (including non-GUI tests)...'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') -Preset windows-release -Deploy 2>&1 | Tee-Object -FilePath (Join-Path $logDir 'windows-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Windows build or tests failed.' }
    Write-Host 'Launching Linux client and signaling server...'
    $remoteCommand = "cd '$LinuxRoot' && nohup env -u ZJDESK_WEBRTC_MAX_BITRATE -u ZJDESK_LIBNICE_ROOT DISPLAY='$Display' XAUTHORITY=/run/user/1000/gdm/Xauthority ZJDESK_WEBRTC_LOCAL_ADDRESS='$LinuxAddress' ZJDESK_WEBRTC_GCC=1 bash ./run.sh > build/start-both.log 2>&1 < /dev/null &"
    & ssh -o BatchMode=yes $LinuxSsh $remoteCommand
    if ($LASTEXITCODE -ne 0) { throw 'Linux launch command failed.' }
    Start-Sleep -Seconds 3
    & ssh -o BatchMode=yes $LinuxSsh "ss -lnt | grep -q ':8765 ' && pgrep -f '[b]uild/linux-release/bin/zjdesk --server ' >/dev/null"
    if ($LASTEXITCODE -ne 0) { throw 'Linux client or server did not start; inspect build/start-both.log on Linux.' }
    Write-Host "Launching Windows client. Server: ws://${LinuxAddress}:8765; room: $Room"
    $env:ZJDESK_WEBRTC_LOCAL_ADDRESS = $WindowsAddress
    $env:ZJDESK_WEBRTC_GCC = '1'
    Remove-Item Env:ZJDESK_WEBRTC_MAX_BITRATE -ErrorAction SilentlyContinue
    & (Join-Path $PSScriptRoot 'run.ps1') -Preset windows-release -ServerUrl "ws://${LinuxAddress}:8765" -Room $Room
} catch {
    Write-Host $_ -ForegroundColor Red
    exit 1
}
