#!/usr/bin/env bash
set -euo pipefail
archive=$(realpath -e -- "$1")
destination=$2
parent=$(cd -- "$(dirname -- "$destination")" && pwd -P)
name=$(basename -- "$destination")
if [[ "$parent" != */build || ! "$name" =~ ^start-both-cache\.[0-9a-f]{16}$ || -L "$destination" ]]; then
    echo 'Refusing to synchronize outside a managed build cache.' >&2
    exit 2
fi
if ! command -v rsync >/dev/null; then
    echo 'Incremental source synchronization requires rsync on Linux.' >&2
    exit 1
fi
mkdir -p -- "$destination"
destination=$(cd -- "$destination" && pwd -P)
if [[ "$destination" != "$parent/$name" ]]; then
    echo 'Build cache path resolves outside its expected directory.' >&2
    exit 2
fi
staging=$(mktemp -d "$parent/start-both-sync.XXXXXXXX")
trap 'rm -rf -- "$staging"' EXIT
python3 -m zipfile -e "$archive" "$staging"
# Compare content, not ZIP extraction timestamps. Do not use --times: unchanged
# sources must retain their existing timestamps so Ninja can reuse object files.
rsync --recursive --checksum --delete \
    --exclude='/build/' --exclude='/dist/' --exclude='/.git/' \
    "$staging/" "$destination/"
printf 'Incremental source synchronized: %s\n' "$destination"
