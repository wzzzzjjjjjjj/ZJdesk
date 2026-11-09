#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# 只模拟网络查询，脚本本身仍由 POSIX shell 执行。
ip() {
    case "$*" in
        '-4 -o addr show up scope global') printf '%s\n' "$fixture_addresses" ;;
        '-4 route show default') printf '%s\n' "$fixture_routes" ;;
        *) return 1 ;;
    esac
}
test() {
    case "$*" in
        '-e /sys/class/net/eth0/device'|'-e /sys/class/net/wlan0/device') return 0 ;;
        '-e /sys/class/net/'*) return 1 ;;
        *) command test "$@" ;;
    esac
}
assert_output() {
    actual=$(. "$project_root/tools/ip-info.sh")
    expected=$(printf '同局域网：%s\n不同局域网：%s\n' "$1" "$2")
    if [ "$actual" != "$expected" ]; then
        printf 'Unexpected script output: %s\n' "$actual" >&2
        exit 1
    fi
}
fixture_addresses='2: eth0 inet 10.11.42.4/24 scope global eth0
2: eth0 inet 10.11.42.5/24 scope global secondary eth0
3: tun0 inet 2.0.1.4/24 scope global tun0
4: tailscale0 inet 100.87.60.58/32 scope global tailscale0'
fixture_routes='default dev tun0 metric 1
default via 10.11.42.1 dev eth0 metric 100'
assert_output '10.11.42.4' '100.87.60.58'
fixture_routes='default dev tun0 metric 1'
assert_output '10.11.42.4' '100.87.60.58'
fixture_addresses='3: tun0 inet 2.0.1.4/24 scope global tun0
2: eth0 inet 100.80.1.2/24 scope global eth0'
assert_output '100.80.1.2' '无'
fixture_addresses=''
assert_output '无' '无'
printf 'PASS four Linux address scenarios, exactly two output lines each.\n'
