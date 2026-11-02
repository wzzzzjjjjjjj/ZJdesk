param([Parameter(Mandatory = $true)][string]$WorkRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testRoot = Join-Path $WorkRoot ("integrity-test-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$testExecutable = Join-Path $testRoot 'whoami.exe'
try {
    Copy-Item -LiteralPath "$env:SystemRoot\System32\whoami.exe" -Destination $testExecutable
    $userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & "$env:SystemRoot\System32\icacls.exe" $testExecutable /grant "*$($userSid):(WO)" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare integrity fixture rights.' }
    & "$env:SystemRoot\System32\icacls.exe" $testExecutable /setintegritylevel L | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare low-integrity fixture.' }
    & "$env:SystemRoot\System32\icacls.exe" $testExecutable /remove:g "*$userSid" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not remove fixture setup rights.' }
    $before = & $testExecutable /groups /fo csv
    if ($LASTEXITCODE -ne 0 -or ($before -join "`n") -notmatch 'S-1-16-4096') {
        throw 'Fixture did not run at low integrity.'
    }
    $accessSection = [Security.AccessControl.AccessControlSections]::Access
    $originalAccess = (Get-Acl -LiteralPath $testExecutable).GetSecurityDescriptorSddlForm($accessSection)
    foreach ($attempt in 1..2) {
        & (Join-Path $projectRoot 'cmake\SetWindowsRuntimeIntegrity.ps1') -Executable $testExecutable
        $after = & $testExecutable /groups /fo csv
        if ($LASTEXITCODE -ne 0 -or ($after -join "`n") -notmatch 'S-1-16-8192') {
            throw "Runtime did not run at medium integrity on attempt $attempt."
        }
        if ((Get-Acl -LiteralPath $testExecutable).GetSecurityDescriptorSddlForm($accessSection) -ne $originalAccess) {
            throw 'Runtime relabeling changed the original DACL.'
        }
    }
    Write-Output 'PASS: low runtime becomes medium; repeated setup preserves access permissions.'
} finally {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
