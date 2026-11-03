#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build"
fixture=$(mktemp -d "$root/build/skip-tests.XXXXXXXX")
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/bin" "$fixture/project"
cp "$root/build.sh" "$fixture/project/build.sh"
cat > "$fixture/bin/cmake" <<'EOF'
#!/usr/bin/env bash
printf 'cmake %s\n' "$*" >> "$CALLS"
EOF
cat > "$fixture/bin/ctest" <<'EOF'
#!/usr/bin/env bash
printf 'ctest %s\n' "$*" >> "$CALLS"
EOF
chmod +x "$fixture/bin/cmake" "$fixture/bin/ctest"
export PATH="$fixture/bin:$PATH" CALLS="$fixture/calls"
bash "$fixture/project/build.sh" --deploy --skip-tests
grep -q '^cmake --build --preset linux-release --parallel 4$' "$CALLS"
grep -q '^cmake --install ' "$CALLS"
if grep -q '^ctest ' "$CALLS"; then echo 'Unexpected test execution' >&2; exit 1; fi
: > "$CALLS"
bash "$fixture/project/build.sh"
grep -q "^ctest --preset linux-release -E \^desktop\$$" "$CALLS"
echo 'Linux build skip/default test checks passed.'
