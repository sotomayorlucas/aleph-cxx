#!/usr/bin/env bash
# Run the bench and assert each baseline is within target. Returns 0 on
# success, 1 if any baseline regressed.
#
# Measurement basis: aleph.cpu:perf CycleCounter (perf_event
# PERF_COUNT_HW_CPU_CYCLES) — true core cycles, frequency-invariant, unlike the
# old rdtscp TSC-tick count. The compute benches measure sustained throughput
# (8 independent accumulators). The bench binary pins itself to a P-core (cpu 2)
# at startup, because a hardware-cycles event reads 0 on the hybrid Core Ultra 7
# 155H's E-cores. Targets below are calibrated for that configuration.
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
