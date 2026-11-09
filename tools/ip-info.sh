#!/bin/sh
# 打印本机可用于连接的 IPv4：同局域网 / 不同局域网（项目使用 Tailscale）
addresses=$(ip -4 -o addr show up scope global 2>/dev/null | awk '
    $4 !~ /^(0\.|127\.|169\.254\.)/ {
        sub(/@.*/, "", $2); sub(/\/.*/, "", $4); print $2, $4
    }')
lan=
# 优先使用有默认路由的物理网卡，VPN 接管默认路由时也保留物理网卡地址。
interfaces=$(ip -4 route show default 2>/dev/null | awk '{
    for (i = 1; i < NF; i++) if ($i == "dev") print $(i + 1)
}')
for interface in $interfaces $(printf '%s\n' "$addresses" | awk '{print $1}'); do
    test -e "/sys/class/net/$interface/device" || continue
    lan=$(printf '%s\n' "$addresses" | awk -v device="$interface" '$1 == device {print $2; exit}')
    [ -z "$lan" ] || break
done
cross=$(printf '%s\n' "$addresses" | awk '$1 ~ /^tailscale/ {print $2; exit}')

printf '同局域网：%s\n' "${lan:-无}"
printf '不同局域网：%s\n' "${cross:-无}"
