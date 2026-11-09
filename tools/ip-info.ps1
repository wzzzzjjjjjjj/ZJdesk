# 打印本机可用于连接的 IPv4：同局域网 / 不同局域网（项目使用 Tailscale）
$ErrorActionPreference = 'SilentlyContinue'
$adapters = @(Get-NetAdapter | Where-Object { $_.Status -eq 'Up' })
$ipv4 = @(Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.AddressState -eq 'Preferred' -and -not $_.SkipAsSource -and
        $_.IPAddress -notmatch '^(0\.|127\.|169\.254\.)' -and
        $_.InterfaceIndex -in $adapters.ifIndex })

$physical = @($adapters | Where-Object { $_.HardwareInterface }).ifIndex
$lan = $null
foreach ($configuration in (Get-NetIPConfiguration |
    Where-Object { $_.InterfaceIndex -in $physical } |
    Sort-Object @{ Expression = { -not [bool]$_.IPv4DefaultGateway } },
        @{ Expression = { $_.NetIPv4Interface.InterfaceMetric } })) {
    $lan = ($ipv4 | Where-Object { $_.InterfaceIndex -eq $configuration.InterfaceIndex } |
        Select-Object -First 1).IPAddress
    if ($lan) { break }
}

$vpn = @($adapters | Where-Object { $_.Name -match 'Tailscale' -or $_.InterfaceDescription -match 'Tailscale' })
$cross = ($ipv4 | Where-Object { $_.InterfaceIndex -in $vpn.ifIndex } |
    Select-Object -First 1).IPAddress

if (-not $lan) { $lan = '无' }
if (-not $cross) { $cross = '无' }

"同局域网：$lan"
"不同局域网：$cross"
