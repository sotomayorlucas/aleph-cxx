# Frequency-Invariant Benchmark Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bench harness's frequency-dependent `rdtscp` measurement with a true per-op core-cycle counter (`perf_event`), restructure the four compute microbenchmarks to measure throughput instead of dependency latency, pin to a P-core, and recalibrate the six gated targets against honest numbers.

**Architecture:** Add a `CycleCounter` RAII wrapper as a new `aleph.cpu:perf` module partition (own-thread `PERF_COUNT_HW_CPU_CYCLES`). The bench harness (`bench/bench_harness.hpp`) uses it in place of `rdtscp`. The four latency-bound compute loops in `bench/bench_main.cpp` become K=8 independent-accumulator throughput loops. `run-baselines.sh` pins the binary to a P-core and gets recalibrated targets measured empirically.

**Tech Stack:** GCC 16 `-std=c++26`, CMake + Ninja, C++20 modules, Linux `perf_event_open`, doctest, AVX2/FMA. Target machine: Intel Core Ultra 7 155H (hybrid, `perf_event_paranoid=2`).

**Spec:** `docs/superpowers/specs/2026-05-30-aleph-cxx-bench-perf-harness-design.md`

## File structure

- Create: `foundation/src/aleph.cpu/aleph.cpu-perf.cppm` — `CycleCounter` (perf_event core-cycle reader).
- Modify: `foundation/src/aleph.cpu/aleph.cpu.cppm` — `export import :perf;`.
- Modify: `foundation/src/aleph.cpu/CMakeLists.txt` — add the new `.cppm` to the module set.
- Modify: `tests/cpu/test_cpu.cpp` — `CycleCounter` unit test.
- Modify: `bench/bench_harness.hpp` — `CycleCounter` replaces `rdtscp`.
- Modify: `bench/bench_main.cpp` — throughput rewrite of 4 compute benches.
- Modify: `bench/run-baselines.sh` — pin to P-core; recalibrated `TARGETS`.

Pre-existing uncommitted work in the tree (from this session, already verified green): the 14-file warnings fix and the spec doc. Task 1 lands these on a branch first so the harness work commits cleanly on top.

---

## Task 1: Setup — branch and commit pre-existing verified work

**Files:** none (git hygiene only).

- [ ] **Step 1: Create a feature branch** (repo is on `main`)

```bash
cd /home/lkz/aleph-cxx
git checkout -b bench-perf-harness
```

- [ ] **Step 2: Commit the already-verified warnings fix** (0 warnings, ctest 14/14 — verified this session)

```bash
git add apps/aleph_sw/main.cpp foundation/src/aleph.io/aleph.io-obj.cppm \
  render/src/aleph.render.rt/aleph.render.rt-path_trace.cppm \
  render/src/aleph.render.sw/aleph.render.sw-clip.cppm \
  render/src/aleph.render.sw/aleph.render.sw-lightmap.cppm \
  render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm \
  render/src/aleph.window/aleph.window-window.cppm \
  tests/CMakeLists.txt tests/containers/test_small_vector.cpp \
  tests/graph/test_invariants_1_5.cpp tests/graph/test_invariants_6_10.cpp \
  tests/render/test_sw_clip.cpp tests/threads/test_pool.cpp tests/tla_cxx_sync.cpp
git commit -m "$(cat <<'EOF'
warnings: zero out release-strict warnings (sign-conversion, nodiscard, doctest -isystem)

Fix all -Wsign-conversion sites in shipped libs (io/obj, render.sw clip/
rasterize/lightmap, window, render.rt path_trace) and app (aleph_sw) by making
local index/count vars std::size_t; (void)-discard the [[nodiscard]]
std::expected returns in graph invariant tests; mark third_party as a SYSTEM
include so doctest's -Waddress is silenced. release-strict now builds with 0
warnings; ctest 14/14 still green.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Commit the design spec**

```bash
git add docs/superpowers/specs/2026-05-30-aleph-cxx-bench-perf-harness-design.md \
        docs/superpowers/plans/2026-05-30-aleph-cxx-bench-perf-harness.md
git commit -m "$(cat <<'EOF'
docs: bench perf-harness design spec + implementation plan

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Verify clean tree**

Run: `git status --short`
Expected: empty (all changes committed).

---

## Task 2: `aleph.cpu:perf` — `CycleCounter` (TDD)

**Files:**
- Create: `foundation/src/aleph.cpu/aleph.cpu-perf.cppm`
- Modify: `foundation/src/aleph.cpu/aleph.cpu.cppm`
- Modify: `foundation/src/aleph.cpu/CMakeLists.txt`
- Test: `tests/cpu/test_cpu.cpp`

- [ ] **Step 1: Write the failing test** — append to `tests/cpu/test_cpu.cpp`

Add `#include <cstdint>` near the top (after `#include "doctest.h"`), then append:

```cpp
TEST_CASE("CycleCounter measures nonzero, plausible core cycles") {
    CycleCounter ctr;
    constexpr std::uint64_t N = 1'000'000;
    std::uint64_t acc = 0;
    ctr.start();
    for (std::uint64_t i = 0; i < N; ++i) {
        acc += i;
        asm volatile("" : "+r"(acc));  // prevent the loop being optimized away
    }
    const std::uint64_t cyc = ctr.stop();
    CHECK(cyc > 0);
    CHECK(cyc < 100 * N);   // sane upper bound: < 100 cyc per trivial add
}
```

- [ ] **Step 2: Run the test to verify it fails to compile**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -20`
Expected: FAIL — `CycleCounter` is not a member of `aleph::cpu` (the partition doesn't exist yet).

- [ ] **Step 3: Create the module partition** — `foundation/src/aleph.cpu/aleph.cpu-perf.cppm`

```cpp
module;
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

export module aleph.cpu:perf;

export namespace aleph::cpu {

// Frequency-invariant per-thread core-cycle counter backed by a single
// perf_event (PERF_COUNT_HW_CPU_CYCLES). Unlike rdtscp — which counts the
// fixed-rate TSC and therefore conflates frequency with work — this counts
// actual CPU_CLK_UNHALTED cycles regardless of turbo/DVFS, the correct unit
// for microbenchmarks. Linux only; requires perf_event_paranoid <= 2.
// Hard-errors (throws) if the event cannot be opened — this is a local
// development gate, not portable infrastructure.
class CycleCounter {
public:
    CycleCounter() {
        perf_event_attr attr{};
        attr.type           = PERF_TYPE_HARDWARE;
        attr.size           = sizeof(attr);
        attr.config         = PERF_COUNT_HW_CPU_CYCLES;
        attr.disabled       = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        fd_ = static_cast<int>(
            ::syscall(__NR_perf_event_open, &attr, /*pid=*/0, /*cpu=*/-1,
                      /*group_fd=*/-1, /*flags=*/0UL));
        if (fd_ < 0) {
            throw std::runtime_error(
                std::string{"CycleCounter: perf_event_open failed: "} +
                std::strerror(errno));
        }
    }

    CycleCounter(const CycleCounter&)            = delete;
    CycleCounter& operator=(const CycleCounter&) = delete;

    ~CycleCounter() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Reset the counter to 0 and begin counting.
    void start() noexcept {
        ::ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
        ::ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
    }

    // Stop counting and return cycles elapsed since the last start().
    std::uint64_t stop() noexcept {
        ::ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
        std::uint64_t count = 0;
        const ssize_t n = ::read(fd_, &count, sizeof(count));
        return (n == static_cast<ssize_t>(sizeof(count))) ? count : 0;
    }

private:
    int fd_{-1};
};

}  // namespace aleph::cpu
```

- [ ] **Step 4: Export the partition from the primary module unit** — `foundation/src/aleph.cpu/aleph.cpu.cppm`

Replace the file with:

```cpp
export module aleph.cpu;
export import :isa;
export import :cycles;
export import :hints;
export import :perf;
```

- [ ] **Step 5: Add the partition to the CMake module set** — `foundation/src/aleph.cpu/CMakeLists.txt`

In the `target_sources(aleph_cpu PUBLIC FILE_SET CXX_MODULES FILES ...)` list, add `aleph.cpu-perf.cppm` after `aleph.cpu-hints.cppm`:

```cmake
add_library(aleph_cpu)
target_sources(aleph_cpu
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.cpu.cppm
        aleph.cpu-isa.cppm
        aleph.cpu-cycles.cppm
        aleph.cpu-hints.cppm
        aleph.cpu-perf.cppm)
# Use aleph_flags_isa (not aleph_flags_strict) so the module BMI dialect
# (exceptions/rtti mode) matches test consumers that use aleph_flags_test.
# GCC hard-errors on cross-dialect BMI imports.
target_link_libraries(aleph_cpu PRIVATE aleph_flags_isa)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build-release --target aleph_tests && ctest --test-dir build-release -R aleph_tests --output-on-failure 2>&1 | tail -15`
Expected: PASS — the new `CycleCounter` case passes; whole `aleph_tests` suite still green.

- [ ] **Step 7: Confirm no warning regression in the strict build**

Run: `cmake --build build-release-strict 2>&1 | grep -c "warning:"`
Expected: `0` (the partition must compile clean under strict flags).

- [ ] **Step 8: Commit**

```bash
git add foundation/src/aleph.cpu/aleph.cpu-perf.cppm \
        foundation/src/aleph.cpu/aleph.cpu.cppm \
        foundation/src/aleph.cpu/CMakeLists.txt \
        tests/cpu/test_cpu.cpp
git commit -m "$(cat <<'EOF'
aleph.cpu:perf — CycleCounter (perf_event core-cycle reader)

Frequency-invariant per-thread cycle counter (PERF_COUNT_HW_CPU_CYCLES) for
microbenchmarks, replacing TSC-tick counting. RAII over a perf_event fd;
start()/stop() reset+enable / disable+read. + unit test.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Harness — measure core cycles via `CycleCounter`

**Files:**
- Modify: `bench/bench_harness.hpp`

- [ ] **Step 1: Replace the harness with the CycleCounter version** — `bench/bench_harness.hpp`

Replace the entire file with:

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

import aleph.cpu;

namespace aleph_bench {

// Run `f(iters)` repeatedly, measuring true core cycles per iteration via a
// perf_event counter (frequency-invariant), and report the median across
// `n_samples`.
template<typename F>
void bench(std::string_view name, F&& f, int n_samples = 50, std::uint64_t iters = 100000) {
    std::vector<double> cycles_per_op;
    cycles_per_op.reserve(static_cast<std::size_t>(n_samples));

    aleph::cpu::CycleCounter ctr;

    // Warm-up to populate caches and ramp the core frequency.
    for (int w = 0; w < 5; ++w) (void)f(iters);

    for (int s = 0; s < n_samples; ++s) {
        ctr.start();
        auto sink = f(iters);
        const std::uint64_t cyc_total = ctr.stop();
        // Force `sink` to be live so the compiler doesn't elide the work.
        asm volatile("" :: "r"(&sink) : "memory");
        const double cyc = static_cast<double>(cyc_total) / static_cast<double>(iters);
        cycles_per_op.push_back(cyc);
    }
    std::sort(cycles_per_op.begin(), cycles_per_op.end());
    const double median = cycles_per_op[static_cast<std::size_t>(n_samples) / 2];
    std::printf("  %-40.40s  %7.2f cyc/op  (median of %d samples)\n",
                std::string{name}.c_str(), median, n_samples);
}

}  // namespace aleph_bench
```

- [ ] **Step 2: Build the bench binary**

Run: `cmake --build build-bench --target aleph_bench 2>&1 | tail -5`
Expected: builds clean (0 errors).

- [ ] **Step 3: Run it pinned and sanity-check the numbers are real core cycles**

Run: `taskset -c 2 ./build-bench/bench/aleph_bench`
Expected: prints all rows. The compute benches still measure latency chains at this point (loops not yet rewritten), so values will differ from the old TSC figures — that is expected. Just confirm the program runs, prints finite numbers, and does not throw "perf_event_open failed".

- [ ] **Step 4: Commit**

```bash
git add bench/bench_harness.hpp
git commit -m "$(cat <<'EOF'
bench: measure true core cycles via aleph.cpu:perf CycleCounter

Replace the rdtscp pair (fixed-rate TSC, frequency-dependent) with a
perf_event core-cycle counter. cyc/op is now real CPU_CLK_UNHALTED cycles.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Restructure the four compute benches to measure throughput

**Files:**
- Modify: `bench/bench_main.cpp`

Rationale: the existing loops carry a single dependency (`a = a*b`, `v = M*v`, …) so they
measure latency. Using K=8 independent accumulators lets the CPU pipeline independent ops and
measures sustained throughput. Inputs that would degenerate (repeated `M*v`) are kept bounded.

- [ ] **Step 1: Replace the "Rotor compose" block** — `bench/bench_main.cpp` lines 23-31

```cpp
    // Rotor compose — throughput, 8 independent unit-rotor product chains.
    // Unit*unit stays unit-norm, so values never degenerate.
    {
        const Rotor b = from_axis_angle({0, 1, 0}, 0.2f);
        Rotor a[8];
        for (int k = 0; k < 8; ++k)
            a[k] = from_axis_angle({1, 0, 0}, 0.3f + 0.01f * static_cast<float>(k));
        aleph_bench::bench("Rotor compose", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i)
                for (int k = 0; k < 8; ++k) a[k] = a[k] * b;
            float acc = 0.0f;
            for (int k = 0; k < 8; ++k) acc += a[k].s;  // escape all lanes
            return acc;
        });
    }
```

- [ ] **Step 2: Replace the "Vec3 dot" block** — `bench/bench_main.cpp` lines 33-45

```cpp
    // Vec3 dot — throughput, 8 independent accumulate-with-feedback chains.
    // a[k].x = s[k]*1e-6 keeps each dot non-hoistable; s[k] stays small.
    {
        Vec3 a[8];
        Vec3 b[8];
        for (int k = 0; k < 8; ++k) {
            a[k] = Vec3{1 + 0.1f * static_cast<float>(k), 2, 3};
            b[k] = Vec3{4, 5, 6};
        }
        aleph_bench::bench("Vec3 dot", [&](std::uint64_t iters) {
            float s[8]{};
            for (std::uint64_t i = 0; i < iters; ++i)
                for (int k = 0; k < 8; ++k) {
                    s[k] += dot(a[k], b[k]);
                    a[k].x = s[k] * 1e-6f;
                }
            float acc = 0.0f;
            for (int k = 0; k < 8; ++k) acc += s[k];
            return acc;
        });
    }
```

- [ ] **Step 3: Replace the "Vec3 add" block** — `bench/bench_main.cpp` lines 47-55

```cpp
    // Vec3 add — throughput, 8 independent accumulators (each += b each iter,
    // so never hoisted; magnitude ~iters, bounded).
    {
        const Vec3 b{1, 1, 1};
        aleph_bench::bench("Vec3 add", [&](std::uint64_t iters) {
            Vec3 acc[8]{};
            for (std::uint64_t i = 0; i < iters; ++i)
                for (int k = 0; k < 8; ++k) acc[k] = acc[k] + b;
            Vec3 r = acc[0];
            for (int k = 1; k < 8; ++k) r = r + acc[k];
            return r;
        });
    }
```

- [ ] **Step 4: Replace the "Mat4 * Vec4" block** — `bench/bench_main.cpp` lines 57-65

```cpp
    // Mat4 * Vec4 — throughput, 8 independent matvecs accumulated per lane.
    // v[k].x varies with i (so M*v[k] is not hoisted) but stays in [1, ~1.1];
    // acc[k] grows to ~iters, bounded, no inf/denormal.
    {
        const Mat4 M = Mat4::perspective(1.0f, 16.0f/9.0f, 0.1f, 100.0f);
        Vec4 v[8];
        for (int k = 0; k < 8; ++k) v[k] = Vec4{1.0f + 0.1f * static_cast<float>(k), 2, 3, 1};
        aleph_bench::bench("Mat4 * Vec4", [&](std::uint64_t iters) {
            Vec4 acc[8]{};
            for (std::uint64_t i = 0; i < iters; ++i)
                for (int k = 0; k < 8; ++k) {
                    v[k].x = 1.0f + 1e-6f * static_cast<float>(i + static_cast<std::uint64_t>(k));
                    acc[k] = acc[k] + M * v[k];
                }
            Vec4 r = acc[0];
            for (int k = 1; k < 8; ++k) r = r + acc[k];
            return r;
        });
    }
```

(The "Arena allocate(64)" and "MpmcRing" blocks are already independent-op loops — leave them
unchanged. The three ray benches are also unchanged.)

- [ ] **Step 5: Build and run**

Run: `cmake --build build-bench --target aleph_bench && taskset -c 2 ./build-bench/bench/aleph_bench`
Expected: builds clean; prints all rows with finite throughput numbers. Run it 2-3 times — the
six gated rows should be stable to within a few percent.

- [ ] **Step 6: Confirm no strict-build warning regression** (bench builds in every preset)

Run: `cmake --build build-release-strict 2>&1 | grep -c "warning:"`
Expected: `0`.

- [ ] **Step 7: Commit**

```bash
git add bench/bench_main.cpp
git commit -m "$(cat <<'EOF'
bench: throughput loops for Rotor/Vec3 dot/Vec3 add/Mat4*Vec4

Replace single-dependency latency chains with K=8 independent accumulators so
the gated compute benches measure sustained throughput, not dependency latency.
Inputs kept bounded (unit rotors; varying-but-bounded Mat4 input) to avoid
hoisting and denormal slowdowns.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Pin `run-baselines.sh` to a P-core

**Files:**
- Modify: `bench/run-baselines.sh`

- [ ] **Step 1: Pin the default BIN to a P-core and document the basis** — `bench/run-baselines.sh`

Replace the header comment and the `BIN` line (lines 1-7) with:

```bash
#!/usr/bin/env bash
# Run the bench and assert each baseline is within target. Returns 0 on
# success, 1 if any baseline regressed.
#
# Measurement basis: aleph.cpu:perf CycleCounter (perf_event
# PERF_COUNT_HW_CPU_CYCLES) — true core cycles, frequency-invariant. The
# compute benches measure throughput (8 independent accumulators). Pinned to a
# P-core (taskset -c 2) so cycle counts are not perturbed by P<->E migration on
# the hybrid Core Ultra 7 155H. Targets calibrated for that configuration.
set -euo pipefail

BIN="${BIN:-taskset -c 2 ./build-bench/bench/aleph_bench}"
# shellcheck disable=SC2086  # BIN intentionally word-splits (taskset + path)
[[ -x "${BIN##* }" ]] || { echo "missing ${BIN##* } — run cmake --build build-bench first" >&2; exit 2; }
```

Then change the invocation on the next line from `out=$("$BIN")` to (unquoted, so `taskset`
and its args split correctly):

```bash
out=$($BIN)
```

- [ ] **Step 2: Run it (targets not yet recalibrated — failures expected here)**

Run: `cd /home/lkz/aleph-cxx && ./bench/run-baselines.sh; echo "exit=$?"`
Expected: it runs the pinned binary and prints OK/FAIL lines. Some FAILs are still expected —
targets are recalibrated in Task 6. Confirm only that pinning works and the script executes.

- [ ] **Step 3: Commit**

```bash
git add bench/run-baselines.sh
git commit -m "$(cat <<'EOF'
bench: pin run-baselines.sh to a P-core + document measurement basis

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Recalibrate targets and verify the gate is green

**Files:**
- Modify: `bench/run-baselines.sh`

- [ ] **Step 1: Take a clean measurement** (machine quiet, pinned)

Run: `cd /home/lkz/aleph-cxx && for r in 1 2 3; do echo "--- run $r ---"; taskset -c 2 ./build-bench/bench/aleph_bench; done`
Record the median cyc/op for the six gated benches: `Rotor compose`, `Vec3 dot`, `Vec3 add`,
`Mat4 * Vec4`, `Arena allocate(64)`, `MpmcRing<u64,1024> push+pop`. Confirm run-to-run
variation is < 5%.

- [ ] **Step 2: Set each target per the recalibration rule** — `bench/run-baselines.sh`, the `TARGETS` map

For each bench: if the measured value meets the **original** target (Rotor 6, Vec3 dot 3,
Vec3 add 3, Mat4*Vec4 8, Arena 3, MPMC 60), keep it. Otherwise set the target to
`ceil(measured × 1.15)` and add a trailing comment with the measured value. For example, if
`Rotor compose` measures 11.2 cyc, set `["Rotor compose"]=13  # measured 11.2`. Update the
`declare -A TARGETS=( ... )` block accordingly using the numbers from Step 1.

```bash
# Edit the TARGETS associative array in bench/run-baselines.sh so every entry
# is >= its measured cyc/op (original target if already met, else ceil(x1.15)).
# Add "# measured <value>" comments for any target you changed.
```

- [ ] **Step 3: Verify the gate is now green**

Run: `cd /home/lkz/aleph-cxx && ./bench/run-baselines.sh; echo "exit=$?"`
Expected: every line prints `OK   ...`, and `exit=0`.

- [ ] **Step 4: Full regression — rebuild all presets, ctest, smokes, baselines**

```bash
cd /home/lkz/aleph-cxx
cmake --build build-release-strict 2>&1 | grep -c "warning:"            # expect 0
cmake --build build-release && ctest --test-dir build-release --output-on-failure 2>&1 | tail -5  # expect 100% pass
taskset -c 2 ./build-release/apps/aleph_rt/aleph_rt /tmp/cornell.ppm cornell 20 10 42 8 && ls -la /tmp/cornell.ppm  # >= 10 KB
./build-release/apps/aleph_graph_fixture/aleph_graph_fixture; echo "fixture exit=$?"  # exit 0
./bench/run-baselines.sh; echo "baselines exit=$?"                       # exit 0
```

Expected: 0 warnings, ctest 100% pass, cornell PPM produced, fixture exit 0, baselines exit 0.

- [ ] **Step 5: Commit**

```bash
git add bench/run-baselines.sh
git commit -m "$(cat <<'EOF'
bench: recalibrate baseline targets to true-cycle throughput numbers

Targets now reflect perf_event core-cycle throughput measured pinned to a
P-core on the 155H. run-baselines.sh exits 0; the perf gate is meaningful and
green.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Self-review notes

- **Spec coverage:** §4.1 CycleCounter → Task 2; §4.2 harness swap → Task 3; §4.3 throughput loops → Task 4; §4.4 pinning → Task 5; §5 recalibration → Task 6; §6 testing → Task 2 (unit) + Task 6 (integration/regression). All spec sections are covered.
- **Note on ASan:** the spec's regression check assumes the `asan` preset can link, which needs `libasan` installed on the host (`sudo dnf install -y libasan`) — tracked separately from this plan; not a gate for the harness work.
- **Note on K:** K=8 is the starting independent-accumulator count. If a compute bench still looks latency-bound after Task 4 (Step 5 numbers implausibly high), raising K to 16 is the tuning lever — but watch for register spills turning it memory-bound.
