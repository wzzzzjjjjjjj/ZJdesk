$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot '../../tools/ip-info.ps1'
# 使用实际网卡形态，覆盖代理虚拟网卡和一张物理网卡的多个地址。
$fixtureAdapters = @(
    [pscustomobject]@{ Name = 'Ethernet'; ifIndex = 1; Status = 'Up'; HardwareInterface = $true },
    [pscustomobject]@{ Name = 'Proxy'; InterfaceDescription = 'Wintun'; ifIndex = 2; Status = 'Up'; HardwareInterface = $false },
    [pscustomobject]@{ Name = 'Tailscale'; ifIndex = 3; Status = 'Up'; HardwareInterface = $false },
    [pscustomobject]@{ Name = 'Wi-Fi'; ifIndex = 4; Status = 'Disconnected'; HardwareInterface = $true }
)
$fixtureConfigurations = @(
    [pscustomobject]@{ InterfaceIndex = 2; IPv4DefaultGateway = '2.0.1.1'; NetIPv4Interface = @{ InterfaceMetric = 1 } },
    [pscustomobject]@{ InterfaceIndex = 1; IPv4DefaultGateway = '192.168.3.1'; NetIPv4Interface = @{ InterfaceMetric = 25 } },
    [pscustomobject]@{ InterfaceIndex = 4; IPv4DefaultGateway = '10.0.0.1'; NetIPv4Interface = @{ InterfaceMetric = 1 } }
)
$fixtureAddresses = @(
    [pscustomobject]@{ InterfaceIndex = 2; IPAddress = '2.0.1.4'; AddressState = 'Preferred'; SkipAsSource = $false },
    [pscustomobject]@{ InterfaceIndex = 1; IPAddress = '192.168.3.5'; AddressState = 'Deprecated'; SkipAsSource = $false },
    [pscustomobject]@{ InterfaceIndex = 1; IPAddress = '192.168.3.7'; AddressState = 'Preferred'; SkipAsSource = $true },
    [pscustomobject]@{ InterfaceIndex = 1; IPAddress = '192.168.3.6'; AddressState = 'Preferred'; SkipAsSource = $false },
    [pscustomobject]@{ InterfaceIndex = 1; IPAddress = '192.168.3.8'; AddressState = 'Preferred'; SkipAsSource = $false },
    [pscustomobject]@{ InterfaceIndex = 3; IPAddress = '100.122.3.99'; AddressState = 'Preferred'; SkipAsSource = $false },
    [pscustomobject]@{ InterfaceIndex = 4; IPAddress = '10.0.0.8'; AddressState = 'Preferred'; SkipAsSource = $false }
)
function Get-NetAdapter { $fixtureAdapters }
function Get-NetIPAddress { param($AddressFamily) $fixtureAddresses }
function Get-NetIPConfiguration { $fixtureConfigurations }
function Assert-Output([string]$Lan, [string]$Cross) {
    $actual = @(& $scriptPath)
    $expected = @("同局域网：$Lan", "不同局域网：$Cross")
    if ($actual.Count -ne 2 -or ($actual -join "`n") -cne ($expected -join "`n")) {
        throw "Unexpected script output: $($actual -join ' / ')"
    }
}
Assert-Output '192.168.3.6' '100.122.3.99'
$fixtureAddresses = @($fixtureAddresses | Where-Object { $_.InterfaceIndex -ne 3 })
Assert-Output '192.168.3.6' '无'
$fixtureConfigurations[1].IPv4DefaultGateway = $null
Assert-Output '192.168.3.6' '无'
$fixtureAddresses = @()
Assert-Output '无' '无'
Write-Host 'PASS four Windows address scenarios, exactly two output lines each.'
