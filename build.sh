#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
preset=linux-release
gui=false
tests=true
deploy=false
media=ON
while (($#)); do
    case "$1" in
        --preset) preset=$2; shift 2 ;;
        --gui-tests) gui=true; shift ;;
        --skip-tests) tests=false; shift ;;
        --deploy) deploy=true; shift ;;
        --without-media) media=OFF; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
case "$preset" in linux-debug|linux-release) ;; *) exit 2 ;; esac
export QT_ROOT=${QT_ROOT:-"$HOME/.local/opt/zjdesk-deps/qt-6.8.3"}
export GST_ROOT=${GST_ROOT:-"$HOME/.local/opt/zjdesk-deps/gstreamer-1.26.11"}
export PATH="$HOME/.local/bin:$QT_ROOT/bin:$GST_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$QT_ROOT/lib:$GST_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$GST_ROOT/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export GST_PLUGIN_SYSTEM_PATH_1_0="$GST_ROOT/lib/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0=''
export GST_PLUGIN_SCANNER="$GST_ROOT/libexec/gstreamer-1.0/gst-plugin-scanner"
mkdir -p "$root/build"
export GST_REGISTRY_1_0="$root/build/gst-registry-linux.bin"
cd "$root"
cmake --preset "$preset" -DZJDESK_ENABLE_GSTREAMER="$media" -DZJDESK_GUI_TESTS="$([[ $gui == true ]] && echo ON || echo OFF)"
cmake --build --preset "$preset" --parallel 4
if $tests; then
    if $gui; then ctest --preset "$preset"; else ctest --preset "$preset" -E '^desktop$'; fi
else
    echo 'Tests skipped.'
fi
if $deploy; then cmake --install "build/$preset" --prefix "$root/dist/$preset"; fi
