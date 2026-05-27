#!/usr/bin/env bash
# Run the bench and assert each baseline is within target. Returns 0 on
# success, 1 if any baseline regressed.
set -euo pipefail

BIN="${BIN:-./build-bench/bench/aleph_bench}"
[[ -x "$BIN" ]] || { echo "missing $BIN — run cmake --build build-bench first" >&2; exit 2; }

out=$("$BIN")
echo "$out"

declare -A TARGETS=(
    ["Rotor compose"]=6
    ["Vec3 dot"]=3
    ["Vec3 add"]=3
    ["Mat4 * Vec4"]=8
    ["Arena allocate(64)"]=3
    ["MpmcRing<u64,1024> push+pop"]=60
)

fail=0
for name in "${!TARGETS[@]}"; do
    target="${TARGETS[$name]}"
    line=$(echo "$out" | grep -F "$name" | head -1)
    if [[ -z "$line" ]]; then
        echo "MISSING bench: $name" >&2
        fail=1
        continue
    fi
    cyc=$(echo "$line" | awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9.]+$/) {print $i; exit}}')
    awk -v c="$cyc" -v t="$target" -v n="$name" \
        'BEGIN { if (c+0 > t+0) { printf "FAIL %s: %s cyc > %s target\n", n, c, t; exit 1 } else { printf "OK   %s: %s cyc <= %s target\n", n, c, t } }' \
        || fail=1
done
exit "$fail"
