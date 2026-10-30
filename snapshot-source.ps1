param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [switch]$Verify
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = $PSScriptRoot
$git = Get-Command git.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $git) { $git = Join-Path $env:ProgramFiles 'Git\cmd\git.exe' }
if (-not (Test-Path -LiteralPath $git -PathType Leaf)) { throw 'Git for Windows was not found.' }
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $git
$start.Arguments = 'ls-files -z --cached --others --exclude-standard'
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.StandardOutputEncoding = [System.Text.Encoding]::UTF8
$process = [System.Diagnostics.Process]::Start($start)
try {
    $output = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'Could not list source files.' }
} finally { $process.Dispose() }
$sources = @{}
foreach ($name in ($output -split "`0" | Where-Object { $_ } | Sort-Object -Unique)) {
    $path = Join-Path $root $name
    $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Source links are not supported: $name" }
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $sources[$name] = $path
    }
}
if ($Verify) {
    $zip = [IO.Compression.ZipFile]::Open($Archive, [IO.Compression.ZipArchiveMode]::Read, [Text.Encoding]::UTF8)
    try {
        if ($zip.Entries.Count -ne $sources.Count) { throw 'Source file list changed during the build.' }
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            foreach ($entry in $zip.Entries) {
                if (-not $sources.ContainsKey($entry.FullName)) { throw "Source file list changed: $($entry.FullName)" }
                $saved = $null
                $current = $null
                try {
                    $saved = $entry.Open()
                    $current = [IO.File]::OpenRead($sources[$entry.FullName])
                    $savedHash = [Convert]::ToBase64String($sha.ComputeHash($saved))
                    $currentHash = [Convert]::ToBase64String($sha.ComputeHash($current))
                    if ($savedHash -ne $currentHash) { throw "Source changed during the build: $($entry.FullName)" }
                } finally {
                    if ($saved) { $saved.Dispose() }
                    if ($current) { $current.Dispose() }
                }
            }
        } finally { $sha.Dispose() }
    } finally { $zip.Dispose() }
} else {
    if (Test-Path -LiteralPath $Archive) { Remove-Item -LiteralPath $Archive -Force }
    $zip = [IO.Compression.ZipFile]::Open($Archive, [IO.Compression.ZipArchiveMode]::Create, [Text.Encoding]::UTF8)
    try {
        foreach ($name in $sources.Keys) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $sources[$name], $name) | Out-Null
        }
    } finally { $zip.Dispose() }
}
