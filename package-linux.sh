#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ${1:-} != --skip-build ]]; then bash "$root/build.sh" --preset linux-release --skip-tests; fi
export QT_ROOT=${QT_ROOT:-"$HOME/.local/opt/zjdesk-deps/qt-6.8.3"}
export GST_ROOT=${GST_ROOT:-"$HOME/.local/opt/zjdesk-deps/gstreamer-1.26.11"}
: "${APPIMAGETOOL:?Set APPIMAGETOOL to the appimagetool x86_64 AppImage path}"
version=$(sed -n 's/^project(ZJdesk VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
mkdir -p "$root/build/packaging-linux" "$root/dist/packages"
appdir=$(mktemp -d "$root/build/packaging-linux/ZJdesk.XXXXXXXX")
python3 "$root/packaging/bundle-linux.py" "$root" "$appdir"
export ARCH=x86_64
export VERSION="$version"
runtime_args=()
if [[ -n ${APPIMAGE_RUNTIME_FILE:-} ]]; then runtime_args+=(--runtime-file "$APPIMAGE_RUNTIME_FILE"); fi
"$APPIMAGETOOL" --appimage-extract-and-run "${runtime_args[@]}" "$appdir" "$root/dist/packages/ZJdesk-$version-linux-x86_64.AppImage"
sha256sum "$root/dist/packages/ZJdesk-$version-linux-x86_64.AppImage"
printf 'AppDir: %s\n' "$appdir"
