$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testRoot = Join-Path $projectRoot ('build\snapshot-line-endings-tests.' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'snapshot-source.ps1') -Destination $testRoot
Push-Location $testRoot
try {
    & git init -q
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize test repository.' }
    [IO.File]::WriteAllText((Join-Path $testRoot 'tracked.sh'), "#!/bin/sh`necho ok`n")
    & git add -- tracked.sh
    if ($LASTEXITCODE -ne 0) { throw 'Could not stage test script.' }
    [IO.File]::WriteAllText((Join-Path $testRoot 'untracked.sh'), "#!/bin/sh`necho ok`n")
    $archive = Join-Path $projectRoot ('build\snapshot-line-endings-' + [guid]::NewGuid().ToString('N') + '.zip')
    & (Join-Path $testRoot 'snapshot-source.ps1') -Archive $archive
    & (Join-Path $testRoot 'snapshot-source.ps1') -Archive $archive -Verify
    foreach ($name in @('tracked.sh', 'untracked.sh')) {
        $path = Join-Path $testRoot $name
        [IO.File]::WriteAllText($path, "#!/bin/sh`r`necho bad`r`n")
        $rejected = $false
        try { & (Join-Path $testRoot 'snapshot-source.ps1') -Archive $archive }
        catch {
            if ($_.Exception.Message -notlike "Shell script has CRLF line endings: $name.*") { throw }
            $rejected = $true
        }
        if (-not $rejected) { throw "Snapshot accepted CRLF in $name." }
        [IO.File]::WriteAllText($path, "#!/bin/sh`necho ok`n")
        Write-Host "PASS snapshot rejects CRLF in $name."
    }
    & (Join-Path $testRoot 'snapshot-source.ps1') -Archive $archive -Verify
    Write-Host 'PASS LF snapshot and verification.'
} finally { Pop-Location }
