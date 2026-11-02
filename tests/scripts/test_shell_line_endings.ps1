$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $projectRoot
try {
    $scripts = @(& git ls-files -- '*.sh')
    if ($LASTEXITCODE -ne 0 -or $scripts.Count -eq 0) { throw 'Could not list tracked shell scripts.' }
    $failures = 0
    foreach ($script in $scripts) {
        $attribute = & git check-attr eol -- $script
        if ($LASTEXITCODE -ne 0 -or $attribute -ne "${script}: eol: lf") {
            Write-Host "FAIL $script does not require LF in Git."
            $failures++
        }
        if ([IO.File]::ReadAllText((Join-Path $projectRoot $script)).Contains("`r`n")) {
            Write-Host "FAIL $script contains CRLF."
            $failures++
        }
    }
    if ($failures) { throw "$failures shell line-ending check(s) failed." }
    Write-Host "PASS $($scripts.Count) shell scripts require and use LF."
} finally { Pop-Location }
