<#
.SYNOPSIS
    Package the newest Windows build as a self-extracting EXE and the Linux build as an
    AppImage, then replace the old files in the output folder next to this script.

.DESCRIPTION
    Windows: calls package-windows.ps1 (builds and deploys windows-release by default),
    which produces the self-extracting EXE. Reuse an existing deployment with
    -ReuseWindowsBuild.

    Linux: AppImage can only be built on Linux, because packaging/bundle-linux.py needs
    ldd, dpkg-query and appimagetool. This script syncs the current source tree to the
    Linux build host over SSH, uploads appimagetool plus the type2 runtime, runs
    package-linux.sh there, and copies the AppImage back.

    Both packages must be produced before the output folder is touched, so a failed run
    never deletes a working package. The folder keeps exactly one EXE and one AppImage
    (versioned file names); older versions of the same kind are removed.

.PARAMETER LinuxSsh
    SSH target of the Linux build host. Default orbbec@10.11.42.4, key-based login required.

.PARAMETER LinuxRoot
    Remote build directory. Default is ZJdesk-release in the remote home directory.
    Its build/ folder is preserved, so later runs compile incrementally.

.PARAMETER AppImageTool
    Local appimagetool path. Default build/packaging-tools/appimagetool-x86_64.AppImage;
    downloaded from the official continuous release when missing.

.PARAMETER RuntimeFile
    Local AppImage type2 runtime path. Default build/packaging-tools/runtime-x86_64;
    downloaded from the official continuous release when missing.

.PARAMETER QtLicenseRoot
    Qt LICENSES directory passed to package-windows.ps1.
    Default build/packaging-tools/qt-licenses.

.PARAMETER SkipWindows
    Skip the Windows EXE and only build the AppImage.

.PARAMETER SkipLinux
    Skip the AppImage and only build the Windows EXE.

.PARAMETER ReuseWindowsBuild
    Do not rebuild Windows; package the existing dist/windows-release deployment.

.PARAMETER ReuseLinuxBuild
    Do not rebuild on Linux; package the existing build/linux-release there.

.EXAMPLE
    .\package-release.ps1

.EXAMPLE
    .\package-release.ps1 -LinuxSsh orbbec@192.168.2.100

.EXAMPLE
    .\package-release.ps1 -SkipLinux
#>
param(
    [string]$LinuxSsh = 'orbbec@10.11.42.4',
    [string]$LinuxRoot = '',
    [string]$AppImageTool = '',
    [string]$RuntimeFile = '',
    [string]$QtLicenseRoot = '',
    [switch]$SkipWindows,
    [switch]$SkipLinux,
    [switch]$ReuseWindowsBuild,
    [switch]$ReuseLinuxBuild
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
# Output folder name is built from code points so this script stays pure ASCII and does
# not depend on the file encoding cmd.exe or PowerShell picks up.
$OutputDir = Join-Path $ProjectRoot (-join ([char[]]@(0x53EF, 0x6267, 0x884C, 0x7A0B, 0x5E8F)))
$PackagesDir = Join-Path $ProjectRoot 'dist/packages'
$version = [regex]::Match((Get-Content (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw), 'project\(ZJdesk VERSION ([\d.]+)').Groups[1].Value
if (-not $version) { throw 'Could not read the ZJdesk version from CMakeLists.txt.' }
if ($SkipWindows -and $SkipLinux) { throw 'SkipWindows and SkipLinux cannot be used together.' }
if (-not $QtLicenseRoot) { $QtLicenseRoot = Join-Path $ProjectRoot 'build/packaging-tools/qt-licenses' }

New-Item -ItemType Directory -Force -Path $PackagesDir | Out-Null
$produced = New-Object System.Collections.Generic.List[string]

if (-not $SkipWindows) {
    Write-Host "==> Windows self-extracting EXE (ZJdesk $version)"
    $packaging = Join-Path $ProjectRoot 'package-windows.ps1'
    if ($ReuseWindowsBuild) {
        & $packaging -SkipBuild -QtLicenseRoot $QtLicenseRoot
    }
    else {
        & $packaging -QtLicenseRoot $QtLicenseRoot
    }
    $exe = Join-Path $PackagesDir "ZJdesk-$version-windows-x64.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "Windows EXE was not created: $exe" }
    $produced.Add($exe)
}

if (-not $SkipLinux) {
    Write-Host "==> Linux AppImage on $LinuxSsh (ZJdesk $version)"
    $archive = Join-Path ([IO.Path]::GetTempPath()) ('ZJdesk-source-' + [guid]::NewGuid().ToString('N') + '.zip')
    & (Join-Path $ProjectRoot 'snapshot-source.ps1') -Archive $archive
    try {
        $homeDir = (& ssh -n -o BatchMode=yes -o ConnectTimeout=10 $LinuxSsh 'printf %s $HOME' 2>$null | Select-Object -Last 1)
        $homeDir = if ($homeDir) { $homeDir.Trim() } else { '' }
        if (-not $homeDir) {
            throw "Cannot reach $LinuxSsh over SSH. Set up key-based login first, or pass -LinuxSsh."
        }
        if (-not $LinuxRoot) { $LinuxRoot = "$homeDir/ZJdesk-release" }

        Write-Host "    Syncing sources to $($LinuxSsh):$LinuxRoot"
        $remoteZip = "$LinuxRoot.source.zip"
        & scp -q $archive "$($LinuxSsh):$remoteZip"
        if ($LASTEXITCODE -ne 0) { throw "Source upload failed: $($LinuxSsh):$remoteZip" }

        $syncScript = @'
set -e
command -v rsync >/dev/null || { echo 'rsync is required on the Linux host.' >&2; exit 3; }
mkdir -p __ROOT__
cd __ROOT__
rm -rf .source-stage
mkdir .source-stage
python3 -m zipfile -e __ZIP__ .source-stage
rsync --recursive --checksum --delete --exclude=/build/ --exclude=/dist/ --exclude=/.git/ --exclude=/.source-stage/ .source-stage/ ./
rm -rf .source-stage
rm -f __ZIP__
'@
        $syncScript = $syncScript.Replace('__ROOT__', "'$LinuxRoot'").Replace('__ZIP__', "'$remoteZip'")
        & ssh -n -o BatchMode=yes -o ConnectTimeout=10 $LinuxSsh ($syncScript -replace "`r`n", "`n")
        if ($LASTEXITCODE -ne 0) { throw 'Source synchronization on the Linux host failed.' }

        if (-not $AppImageTool) { $AppImageTool = Join-Path $ProjectRoot 'build/packaging-tools/appimagetool-x86_64.AppImage' }
        if (-not $RuntimeFile) { $RuntimeFile = Join-Path $ProjectRoot 'build/packaging-tools/runtime-x86_64' }
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        if (-not (Test-Path -LiteralPath $AppImageTool -PathType Leaf)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $AppImageTool) | Out-Null
            Write-Host '    Downloading the official appimagetool (about 15 MB, first run only)'
            Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage' -OutFile $AppImageTool
        }
        if (-not (Test-Path -LiteralPath $RuntimeFile -PathType Leaf)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $RuntimeFile) | Out-Null
            Write-Host '    Downloading the official AppImage runtime (about 1 MB, first run only)'
            Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64' -OutFile $RuntimeFile
        }

        $remoteTools = "$LinuxRoot/build/packaging-tools"
        $remoteTool = "$remoteTools/appimagetool-x86_64.AppImage"
        $remoteRuntime = "$remoteTools/runtime-x86_64"
        Write-Host "    Uploading packaging tools to $($LinuxSsh):$remoteTools"
        & ssh -n -o BatchMode=yes $LinuxSsh "mkdir -p '$remoteTools'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the packaging tool directory on the Linux host.' }
        & scp -q $AppImageTool "$($LinuxSsh):$remoteTool"
        if ($LASTEXITCODE -ne 0) { throw 'appimagetool upload failed.' }
        & scp -q $RuntimeFile "$($LinuxSsh):$remoteRuntime"
        if ($LASTEXITCODE -ne 0) { throw 'AppImage runtime upload failed.' }
        & ssh -n -o BatchMode=yes $LinuxSsh "chmod +x '$remoteTool'"
        if ($LASTEXITCODE -ne 0) { throw 'Could not make the remote appimagetool executable.' }

        $buildArgs = if ($ReuseLinuxBuild) { ' --skip-build' } else { '' }
        Write-Host '    Building on the Linux host (the first full build takes a while)'
        $buildScript = "cd '$LinuxRoot' && APPIMAGETOOL='$remoteTool' APPIMAGE_RUNTIME_FILE='$remoteRuntime' bash package-linux.sh$buildArgs"
        & ssh -n -o BatchMode=yes -o ServerAliveInterval=30 $LinuxSsh $buildScript
        if ($LASTEXITCODE -ne 0) { throw 'The Linux AppImage build failed.' }

        $appImageName = "ZJdesk-$version-linux-x86_64.AppImage"
        $remoteAppImage = "$LinuxRoot/dist/packages/$appImageName"
        & scp -q "$($LinuxSsh):$remoteAppImage" (Join-Path $PackagesDir $appImageName)
        if ($LASTEXITCODE -ne 0) { throw "AppImage download failed: $($LinuxSsh):$remoteAppImage" }
        $appImage = Join-Path $PackagesDir $appImageName
        if (-not (Test-Path -LiteralPath $appImage)) { throw "AppImage was not created: $appImage" }
        $produced.Add($appImage)
    }
    finally {
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
foreach ($file in $produced) {
    $pattern = if ($file -like '*.AppImage') { 'ZJdesk-*.AppImage' } else { 'ZJdesk-*.exe' }
    Get-ChildItem -LiteralPath $OutputDir -File -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item -Force
    Copy-Item -LiteralPath $file -Destination (Join-Path $OutputDir (Split-Path $file -Leaf)) -Force
}

Write-Host ''
Write-Host "==> Done. Output folder: $OutputDir"
Get-ChildItem -LiteralPath $OutputDir -File | Sort-Object Name | ForEach-Object {
    Write-Host ('    {0}   {1:N0} bytes   {2:yyyy-MM-dd HH:mm}' -f $_.Name, $_.Length, $_.LastWriteTime)
}
