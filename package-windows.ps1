<# Build a portable ZIP and a self-extracting EXE using the Windows .NET Framework compiler. #>
param([switch]$SkipBuild, [string]$QtLicenseRoot = "$PSScriptRoot/build/packaging-tools/qt-licenses")
$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) { & "$PSScriptRoot/build.ps1" -Preset windows-release -Deploy -SkipTests }
$version = [regex]::Match((Get-Content "$PSScriptRoot/CMakeLists.txt" -Raw), 'project\(ZJdesk VERSION ([\d.]+)').Groups[1].Value
$deployment = Join-Path $PSScriptRoot 'dist/windows-release'
$stage = Join-Path $PSScriptRoot ('build/packaging-windows/' + [guid]::NewGuid().ToString('N'))
$output = Join-Path $PSScriptRoot 'dist/packages'
New-Item -ItemType Directory -Force -Path $stage, $output | Out-Null
foreach ($folder in @('bin', 'plugins', 'translations', 'licenses')) {
    Copy-Item -LiteralPath "$deployment/$folder" -Destination $stage -Recurse
}
if (-not (Test-Path -LiteralPath "$QtLicenseRoot/LGPL-3.0-only.txt")) {
    throw 'Pass -QtLicenseRoot with the LICENSES directory from the Qt 6.8.3 sources.'
}
Copy-Item -LiteralPath $QtLicenseRoot -Destination "$stage/licenses/qt" -Recurse
# App-local MSVC runtime avoids an administrator-level redistributable installation.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$runtime = Get-ChildItem "$vsRoot/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT" -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $runtime) { throw 'MSVC x64 runtime was not found.' }
Copy-Item "$($runtime.FullName)/*.dll" "$stage/bin"
Remove-Item -LiteralPath "$stage/bin/vc_redist.x64.exe" -ErrorAction SilentlyContinue
@'
ZJdesk Windows x64

免安装：解压全部文件后双击 bin/zjdesk.exe，保留其他 DLL 和目录。
单文件 EXE：首次启动解压到 %LOCALAPPDATA%/ZJdesk/packages，之后复用；不需要管理员权限。
默认缓存目录无法写入时，自动改用 EXE 旁的 ZJdesk-cache 目录。
如需调整解压位置，可设置环境变量 ZJDESK_PACKAGE_CACHE。
客户端不会自动启动信令服务。服务机器运行：
  bin/zjdesk-server.exe --listen 0.0.0.0 --port 8765
两端在界面填写 ws://服务机器IP:8765，并输入相同房间码。
适用 Windows 11 x64；其他版本尚未完成干净系统验收。
'@ | Set-Content "$stage/使用说明.txt" -Encoding utf8
$archive = Join-Path $output "ZJdesk-$version-windows-x64-portable.zip"
Compress-Archive -Path "$stage/*" -DestinationPath $archive -CompressionLevel Optimal -Force
$compiler = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
$executable = Join-Path $output "ZJdesk-$version-windows-x64.exe"
$launcherSource = (Resolve-Path -LiteralPath "$PSScriptRoot/packaging/WindowsLauncher.cs").Path
& $compiler /nologo /target:winexe /platform:x64 /optimize+ "/out:$executable" "/win32icon:$PSScriptRoot/packaging/zjdesk.ico" "/resource:$archive,payload.zip" /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Windows.Forms.dll $launcherSource
if ($LASTEXITCODE -ne 0) { throw 'EXE launcher compilation failed.' }
$packageHashes = foreach ($package in (Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Extension -in '.exe', '.zip', '.AppImage' } | Sort-Object Name)) {
    $stream = [System.IO.File]::OpenRead($package.FullName)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        '{0}  {1}' -f ([System.BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()), $package.Name
    }
    finally { $stream.Dispose(); $sha.Dispose() }
}
[System.IO.File]::WriteAllLines((Join-Path $output 'SHA256SUMS.txt'), [string[]]$packageHashes, [System.Text.UTF8Encoding]::new($false))
$packageHashes
Write-Output "Portable staging directory: $stage"
