#requires -Version 7.0
# Exercise launcher failures without opening a GUI or running the application.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$testRoot = Join-Path $root ('build/packaging-validation/launcher-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$source = (Get-Content -LiteralPath "$root/packaging/WindowsLauncher.cs" -Raw -Encoding utf8).Replace("`r`n", "`n")
# Replace only the modal UI with a marker; the extraction and reporting branches remain intact.
if (-not $source.Contains('MessageBox.Show(error.Message, "ZJdesk 启动失败", MessageBoxButtons.OK, MessageBoxIcon.Error);')) {
    throw 'Launcher dialog call changed; update the test interception before running it.'
}
$source = $source.Replace('[STAThread]', '[DllImport("kernel32.dll")] private static extern bool SetStdHandle(int nStdHandle, IntPtr handle);' + "`n    [STAThread]")
$source = $source.Replace("private static int Main(string[] args)`n    {", "private static int Main(string[] args)`n    {`n        SetStdHandle(-12, IntPtr.Zero);")
$source = $source.Replace('MessageBox.Show(error.Message, "ZJdesk 启动失败", MessageBoxButtons.OK, MessageBoxIcon.Error);', 'File.WriteAllText(Environment.GetEnvironmentVariable("ZJDESK_TEST_DIALOG"), error.Message);')
$source = $source.Replace('cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ZJdesk", "packages");', 'cache = Environment.GetEnvironmentVariable("ZJDESK_TEST_DEFAULT_CACHE");')
$source = $source.Replace('Directory.CreateDirectory(temporary);', 'if (Environment.GetEnvironmentVariable("ZJDESK_TEST_DENIED_CACHE") != null && directory.StartsWith(Path.GetFullPath(Environment.GetEnvironmentVariable("ZJDESK_TEST_DENIED_CACHE")), StringComparison.OrdinalIgnoreCase)) throw new UnauthorizedAccessException("Simulated cache permission denied"); Directory.CreateDirectory(temporary);')
$sourcePath = Join-Path $testRoot 'Launcher.cs'
[System.IO.File]::WriteAllText($sourcePath, $source, [System.Text.UTF8Encoding]::new($false))
$payload = Join-Path $testRoot 'invalid.zip'
[System.IO.File]::WriteAllBytes($payload, [byte[]]@(1, 2, 3, 4))
$executable = Join-Path $testRoot 'Launcher.exe'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $compiler /nologo /target:winexe /platform:x64 "/out:$executable" "/resource:$payload,payload.zip" /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Windows.Forms.dll $sourcePath
if ($LASTEXITCODE -ne 0) { throw 'Launcher test compilation failed.' }
$oldCache, $oldDialog = $env:ZJDESK_PACKAGE_CACHE, $env:ZJDESK_TEST_DIALOG
$env:ZJDESK_PACKAGE_CACHE = Join-Path $testRoot 'cache'
$env:ZJDESK_TEST_DIALOG = Join-Path $testRoot 'dialog.txt'
$type = [System.Reflection.Assembly]::LoadFile($executable).GetType('WindowsLauncher')
$flags = [System.Reflection.BindingFlags]'NonPublic,Static'
$stderr = $type.GetMethod('GetStdHandle', $flags).Invoke($null, [object[]]@(-12))
try {
    $exitCode = $type.GetMethod('Main', $flags).Invoke($null, [object[]]@(,[string[]]@()))
    $result = [pscustomobject]@{
        ExitCode = $exitCode
        ErrorDisplayed = Test-Path -LiteralPath $env:ZJDESK_TEST_DIALOG
        LeftoverTemporaryDirectories = @(Get-ChildItem -LiteralPath $env:ZJDESK_PACKAGE_CACHE -Directory).Count
    }
    $result | ConvertTo-Json | Set-Content -LiteralPath "$testRoot/result.json"
    if ($exitCode -ne 1 -or -not $result.ErrorDisplayed -or $result.LeftoverTemporaryDirectories -ne 0) {
        throw "Launcher regression failed: $($result | ConvertTo-Json -Compress)"
    }
    $result

    # A denied default path retries beside the launcher; an explicit path must be respected.
    $env:ZJDESK_TEST_DEFAULT_CACHE = Join-Path $testRoot 'denied'
    $env:ZJDESK_TEST_DENIED_CACHE = $env:ZJDESK_TEST_DEFAULT_CACHE
    $env:ZJDESK_PACKAGE_CACHE = $null
    $exitCode = $type.GetMethod('Main', $flags).Invoke($null, [object[]]@(,[string[]]@()))
    $fallback = Join-Path $testRoot 'ZJdesk-cache'
    $dialog = Get-Content -LiteralPath $env:ZJDESK_TEST_DIALOG -Raw
    if ($exitCode -ne 1 -or -not (Test-Path -LiteralPath $fallback) -or $dialog.Contains('Simulated cache permission denied') -or @(Get-ChildItem -LiteralPath $fallback -Force).Count -ne 0) {
        throw 'A denied default cache did not reach extraction in the fallback directory.'
    }
    Remove-Item -LiteralPath $fallback
    $env:ZJDESK_PACKAGE_CACHE = $env:ZJDESK_TEST_DEFAULT_CACHE
    $exitCode = $type.GetMethod('Main', $flags).Invoke($null, [object[]]@(,[string[]]@()))
    $dialog = Get-Content -LiteralPath $env:ZJDESK_TEST_DIALOG -Raw
    if ($exitCode -ne 1 -or $dialog -ne 'Simulated cache permission denied' -or (Test-Path -LiteralPath $fallback)) {
        throw 'An explicit cache path incorrectly fell back to another directory.'
    }
    Write-Output 'Default cache fallback and explicit cache failure passed.'
}
finally {
    $null = $type.GetMethod('SetStdHandle', $flags).Invoke($null, [object[]]@(-12, $stderr))
    $env:ZJDESK_PACKAGE_CACHE, $env:ZJDESK_TEST_DIALOG = $oldCache, $oldDialog
    Remove-Item Env:ZJDESK_TEST_DEFAULT_CACHE,Env:ZJDESK_TEST_DENIED_CACHE -ErrorAction SilentlyContinue
}
