#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build"
fixture=$(mktemp -d "$root/build/incremental-sync.XXXXXXXX")
trap 'rm -rf -- "$fixture"' EXIT
target="$fixture/build/start-both-cache.0123456789abcdef"
mkdir -p "$target/build" "$target/dist"
printf 'cached object' > "$target/build/object.o"
printf 'deployed binary' > "$target/dist/client"
python3 - "$fixture/source.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as archive:
    archive.writestr('src/same.cpp', 'unchanged')
    archive.writestr('src/changed.cpp', 'first')
    archive.writestr('src/deleted.cpp', 'obsolete')
PY
bash "$root/sync-source.sh" "$fixture/source.zip" "$target"
python3 - "$target" "$fixture/source.zip" <<'PY'
from pathlib import Path
import os, sys, zipfile
target = Path(sys.argv[1])
os.utime(target / 'src/same.cpp', ns=(1_000_000_000, 1_000_000_000))
with zipfile.ZipFile(sys.argv[2], 'w') as archive:
    archive.writestr('src/same.cpp', 'unchanged')
    archive.writestr('src/changed.cpp', 'second')
    archive.writestr('src/中文.h', 'new')
PY
bash "$root/sync-source.sh" "$fixture/source.zip" "$target"
bash "$root/sync-source.sh" "$fixture/source.zip" "$target"
python3 - "$target" <<'PY'
from pathlib import Path
import sys
target = Path(sys.argv[1])
assert (target / 'src/same.cpp').stat().st_mtime_ns == 1_000_000_000
assert (target / 'src/changed.cpp').read_text() == 'second'
assert (target / 'src/中文.h').read_text() == 'new'
assert not (target / 'src/deleted.cpp').exists()
assert (target / 'build/object.o').read_text() == 'cached object'
assert (target / 'dist/client').read_text() == 'deployed binary'
print('PASS unchanged timestamps, modified/new/deleted sources, cached objects and deployment')
PY
mkdir "$fixture/original"
printf 'original' > "$fixture/original/keep.txt"
if bash "$root/sync-source.sh" "$fixture/source.zip" "$fixture/original"; then
    echo 'Accepted an unmanaged source directory' >&2; exit 1
fi
ln -s "$fixture/original" "$fixture/build/start-both-cache.abcdef0123456789"
if bash "$root/sync-source.sh" "$fixture/source.zip" "$fixture/build/start-both-cache.abcdef0123456789"; then
    echo 'Accepted a symlink cache' >&2; exit 1
fi
test "$(cat "$fixture/original/keep.txt")" = original
echo 'PASS original checkout and symlink target remain untouched'
