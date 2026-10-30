#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export QT_ROOT=${QT_ROOT:-"$HOME/.local/opt/zjdesk-deps/qt-6.8.3"}
export GST_ROOT=${GST_ROOT:-"$HOME/.local/opt/zjdesk-deps/gstreamer-1.26.11"}
export PATH="$QT_ROOT/bin:$GST_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$QT_ROOT/lib:$GST_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GST_PLUGIN_SYSTEM_PATH_1_0="$GST_ROOT/lib/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0=''
export GST_PLUGIN_SCANNER="$GST_ROOT/libexec/gstreamer-1.0/gst-plugin-scanner"
export GST_REGISTRY_1_0="$root/build/gst-registry-linux.bin"
if [[ -n ${ZJDESK_LIBNICE_ROOT:-} ]]; then
    candidate_root=$(cd -- "$ZJDESK_LIBNICE_ROOT" && pwd)
    if [[ ! -f "$candidate_root/lib/libnice.so.10" || ! -f "$candidate_root/lib/gstreamer-1.0/libgstnice.so" ]]; then
        printf 'ZJDESK_LIBNICE_ROOT must contain both lib/libnice.so.10 and lib/gstreamer-1.0/libgstnice.so\n' >&2
        exit 1
    fi
    export LD_LIBRARY_PATH="$candidate_root/lib:$LD_LIBRARY_PATH"
    export GST_PLUGIN_PATH_1_0="$candidate_root/lib/gstreamer-1.0"
    export GST_REGISTRY_1_0="$root/build/gst-registry-linux-libnice-candidate.bin"
    printf 'Using isolated libnice runtime: %s\n' "$candidate_root" >&2
fi
export QT_QPA_PLATFORM=xcb
bin="$root/build/linux-release/bin"
server_pid=''
client_pid=''
cleanup() {
    if [[ -n $client_pid ]]; then kill "$client_pid" 2>/dev/null || true; fi
    if [[ -n $server_pid ]]; then kill "$server_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
server=${1:-}
if [[ -z $server ]]; then
    server=ws://127.0.0.1:8765
    "$bin/zjdesk-server" --listen 0.0.0.0 --port 8765 > "$root/build/server.log" 2>&1 &
    server_pid=$!
    sleep 0.4
    kill -0 "$server_pid"
fi
"$bin/zjdesk" --server "$server" --room "${2:-demo}" --display-name "${3:-$(hostname)}" &
client_pid=$!
wait "$client_pid"
