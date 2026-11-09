function Test-ZjdeskTcpPort {
    param([string]$Address, [string]$LocalAddress, [int]$Port = 22)
    $client = [Net.Sockets.TcpClient]::new()
    $pending = $null
    try {
        $client.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Parse($LocalAddress), 0))
        $pending = $client.BeginConnect($Address, $Port, $null, $null)
        if (-not $pending.AsyncWaitHandle.WaitOne(1500)) { return $false }
        $client.EndConnect($pending)
        return $true
    } catch { return $false }
    finally {
        if ($pending) { $pending.AsyncWaitHandle.Dispose() }
        $client.Dispose()
    }
}

function Get-ZjdeskWindowsAddresses {
    $addresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue)
    foreach ($adapter in @(Get-NetAdapter -Physical -ErrorAction SilentlyContinue | Where-Object { $_.Status -eq 'Up' })) {
        $medium = [int]$adapter.NdisPhysicalMedium
        if ($medium -in @(1, 9)) { $kind = 'wireless' }
        elseif ($medium -eq 14 -or ($medium -eq 0 -and "$($adapter.MediaType)" -eq '802.3')) { $kind = 'wired' }
        else { continue }
        foreach ($address in $addresses | Where-Object {
            $_.InterfaceIndex -eq $adapter.ifIndex -and $_.AddressState -eq 'Preferred' -and
            -not $_.SkipAsSource -and $_.IPAddress -notmatch '^(169\.254\.|127\.)'
        }) {
            [pscustomobject]@{
                Address = $address.IPAddress; InterfaceIndex = $adapter.ifIndex
                Interface = $adapter.Name; Kind = $kind
            }
        }
    }
}

function Resolve-ZjdeskNetwork {
    param([string[]]$LinuxSsh, [string]$LinuxAddress, [string]$WindowsAddress)
    $priority = @{ wired = 0; wireless = 1; tailscale = 2 }
    if ($WindowsAddress) {
        $address = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -eq $WindowsAddress } | Select-Object -First 1
        if (-not $address) { throw "Windows media address $WindowsAddress is unavailable." }
        $windowsCandidates = @([pscustomobject]@{
            Address = $WindowsAddress; InterfaceIndex = $address.InterfaceIndex
            Interface = $address.InterfaceAlias; Kind = 'explicit'
        })
    } else {
        $windowsCandidates = @(Get-ZjdeskWindowsAddresses | Sort-Object { $priority[$_.Kind] }, Interface, Address)
        $windowsCandidates += @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.InterfaceAlias -eq 'Tailscale' -and $_.AddressState -eq 'Preferred' -and
                -not $_.SkipAsSource -and $_.IPAddress -notmatch '^(169\.254\.|127\.)' } |
            ForEach-Object {
                [pscustomobject]@{
                    Address = $_.IPAddress; InterfaceIndex = $_.InterfaceIndex
                    Interface = $_.InterfaceAlias; Kind = 'tailscale'
                }
            })
    }
    # Obtain addresses from the actual host, not interface names or old DHCP leases.
    $inventoryCommand = @'
ip -4 -o addr show up scope global | while read -r index iface family cidr rest; do
    iface=${iface%%@*}
    test "$(cat /sys/class/net/"$iface"/operstate)" != down || continue
    kind=other
    if test -d /sys/class/net/"$iface"/wireless; then kind=wireless
    elif test -e /sys/class/net/"$iface"/device; then kind=wired; fi
    printf '%s\t%s\t%s\n' "$kind" "$iface" "${cidr%/*}"
done
'@
    $inventory = @()
    $sshTarget = ''
    foreach ($endpoint in $LinuxSsh) {
        try {
            $output = @(& ssh -o BatchMode=yes -o ConnectTimeout=3 $endpoint ($inventoryCommand -replace "`r`n", "`n") 2>&1)
        } catch { continue }
        if ($LASTEXITCODE -ne 0) { continue }
        foreach ($line in $output) {
            if ("$line" -match '^(wired|wireless|other)\t([^\t]+)\t([0-9.]+)$') {
                $kind = $Matches[1]
                if ($Matches[2] -like 'tailscale*') { $kind = 'tailscale' }
                $inventory += [pscustomobject]@{ Kind = $kind; Interface = $Matches[2]; Address = $Matches[3] }
            }
        }
        if ($inventory.Count) { $sshTarget = $endpoint; break }
    }
    if (-not $sshTarget) { throw 'Could not query Linux network addresses over SSH. Check -LinuxSsh and connectivity.' }
    if ($LinuxAddress) {
        $candidates = @($inventory | Where-Object { $_.Address -eq $LinuxAddress })
        if (-not $candidates.Count) { throw "Linux media address $LinuxAddress is unavailable." }
    } else {
        $candidates = @($inventory | Where-Object { $_.Kind -ne 'other' } |
            Sort-Object { $priority[$_.Kind] }, Interface, Address)
    }
    foreach ($candidate in $candidates) {
        foreach ($local in $windowsCandidates) {
            if (-not $WindowsAddress -and (($candidate.Kind -eq 'tailscale') -ne ($local.Kind -eq 'tailscale'))) { continue }
            $route = Find-NetRoute -RemoteIPAddress $candidate.Address -LocalIPAddress $local.Address `
                -InterfaceIndex $local.InterfaceIndex -ErrorAction SilentlyContinue |
                Where-Object { $_.IPAddress -eq $local.Address } | Select-Object -First 1
            if (-not $route) { continue }
            if (-not (Test-ZjdeskTcpPort -Address $candidate.Address -LocalAddress $local.Address)) { continue }
            return [pscustomobject]@{
                LinuxSsh = $sshTarget; LinuxAddress = $candidate.Address; WindowsAddress = $local.Address
                Interface = $candidate.Interface; Kind = $candidate.Kind
                WindowsInterface = $local.Interface; WindowsKind = $local.Kind
            }
        }
    }
    throw 'No reachable Linux media address with a Windows route. Check Ethernet/Wi-Fi or Tailscale, SSH and -LinuxAddress.'
}
