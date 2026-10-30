#!/usr/bin/env bash
# Build an isolated ICE runtime for A/B tests; never install over the SDK/system.
set -euo pipefail
: "${GST_ROOT:?Set GST_ROOT to the existing GStreamer SDK}"
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
candidate_root=${1:-"$repo_root/build/libnice-candidate"}
mkdir -p "$candidate_root"
candidate_root=$(cd "$candidate_root" && pwd)
archive="$candidate_root/libnice-0.1.24.tar.gz"
source_dir="$candidate_root/libnice-0.1.24"
build_dir="$candidate_root/build"
stage_dir="$candidate_root/stage"
if [[ ! -f "$archive" ]]; then
  curl -fL --connect-timeout 10 --max-time 120 \
    https://libnice.freedesktop.org/releases/libnice-0.1.24.tar.gz -o "$archive"
fi
# Pinned by GStreamer's official 1.28 libnice.wrap.
printf '%s  %s\n' cfb5e8e778534f2f5b3c6f4958a1eb057c6b95c537c0f100817a537cf5d64fcc "$archive" | sha256sum -c -
if [[ ! -d "$source_dir" ]]; then
  tar -xzf "$archive" -C "$candidate_root"
fi
export PKG_CONFIG_PATH="$GST_ROOT/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$GST_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
setup_args=()
if [[ -f "$build_dir/build.ninja" ]]; then
  setup_args+=(--reconfigure)
fi
meson setup "${setup_args[@]}" "$build_dir" "$source_dir" \
  --prefix "$stage_dir" --libdir lib --wrap-mode=nofallback --auto-features=disabled \
  -Dbuildtype=release -Dgstreamer=enabled -Dtests="${ZJDESK_LIBNICE_TESTS:-disabled}" -Dexamples=disabled \
  -Dgupnp=disabled -Dintrospection=disabled -Dgtk_doc=disabled
meson compile -C "$build_dir" -j 4
meson install -C "$build_dir" --no-rebuild
printf '\nCandidate staged at %s\n' "$stage_dir"
printf 'SDK and system libraries were not replaced.\n'
