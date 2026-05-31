# Aleph-cxx — Frequency-Invariant Benchmark Harness

**Status:** approved design, ready for implementation plan.
**Date:** 2026-05-30.
**Branch:** `main` (small, self-contained change).
**Scope:** local development gate on the project's Intel Core Ultra 7 155H (Meteor Lake) machine only.

## 1. Goal

Make the microbenchmark gate (`bench/` + `run-baselines.sh`) measure what it claims to:
**true per-op core cycles, sustained throughput, frequency-invariant.** Then recalibrate the
six gated targets against honest numbers.

The current harness fails this on three counts (diagnosed 2026-05-30):

1. **Wrong unit.** It counts `rdtscp` ticks. On this CPU the TSC ticks at a fixed 2995 MHz
   while the core runs at up to 4.8 GHz turbo, so reported "cyc/op" ≈ `real_cycles × (3.0/F_core)`
   — a frequency-dependent number that is *not* core cycles.
2. **Wrong quantity.** Every gated loop is a latency-bound dependency chain
   (`a = a*b`, `v = M*v`, `s += dot(a,b); a.x = s*1e-6f`), so it measures dependency latency,
   not the throughput the targets (e.g. Vec3 dot ≤ 3 cyc) imply.
3. **Mis-calibrated targets.** As a consequence the six targets are unreachable as written;
   they were committed without a verified green run.

This is a measurement/harness defect, not a code regression: the benchmarked `aleph.math` /
`aleph.alloc` code is byte-identical to when the baselines were committed (task 31).

Out of scope: portability to CI / other machines / non-Linux (explicitly deferred — this is a
local 155H gate); changing any benchmarked library code; adding new benchmarks.

## 2. Feasibility (verified)

On the target machine:

- `kernel.perf_event_paranoid = 2` → unprivileged own-thread hardware-cycle counting is allowed.
- A probe of `perf_event_open(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, pid=0, cpu=-1,
  exclude_kernel=1)` succeeded and returned real core cycles; the kernel handled the hybrid
  `cpu_core`/`cpu_atom` PMU transparently (no manual PMU-type selection needed).
- `rdpmc` userspace flag is not enabled — not needed. `perf_event_open` + `read()` is
  frequency-invariant (counts `CPU_CLK_UNHALTED`) and its syscall overhead amortizes to ~0
  over 100 000 iterations per sample.

## 3. Architecture

```
foundation/src/aleph.cpu/
├── aleph.cpu.cppm                 # primary unit  (+ export import :perf;)
├── aleph.cpu-cycles.cppm          # existing: rdtsc / rdtscp (unchanged)
└── aleph.cpu-perf.cppm            # NEW: CycleCounter (perf_event core-cycle reader)

bench/
├── bench_harness.hpp              # MODIFIED: rdtscp pair -> CycleCounter
├── bench_main.cpp                 # MODIFIED: 6 gated benches -> throughput loops
└── run-baselines.sh               # MODIFIED: pin to P-core; recalibrated TARGETS

tests/cpu/
└── test_cpu.cpp                   # MODIFIED: + CycleCounter unit test
```

The `rdtsc`/`rdtscp` primitives in `:cycles` are left intact — they remain useful elsewhere;
the bench simply stops using them.

## 4. Components

### 4.1 `aleph.cpu:perf` — `CycleCounter`

A small RAII wrapper around a single own-thread hardware-cycle perf event.

- Global module fragment includes: `<linux/perf_event.h>`, `<sys/ioctl.h>`, `<sys/syscall.h>`,
  `<unistd.h>`, `<cstdint>`, `<cerrno>`, `<system_error>` (or equivalent for the throw).
- `perf_event_open` is invoked via `syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0)`
  (no libc wrapper assumed).
- Interface:
  - `CycleCounter()` — opens the event with `type=PERF_TYPE_HARDWARE`,
    `config=PERF_COUNT_HW_CPU_CYCLES`, `disabled=1`, `exclude_kernel=1`, `exclude_hv=1`.
    On `fd < 0`, throw with a message including `errno` (local gate ⇒ hard error, no fallback).
  - `void start() noexcept` — `ioctl(fd, PERF_EVENT_IOC_RESET, 0)` then
    `ioctl(fd, PERF_EVENT_IOC_ENABLE, 0)`.
  - `std::uint64_t stop() noexcept` — `ioctl(fd, PERF_EVENT_IOC_DISABLE, 0)`, then
    `read(fd, &count, sizeof count)`; return `count` (true core cycles since `start`).
  - destructor — `close(fd)`.
  - Non-copyable, non-movable (owns an fd). The harness constructs one as a local in each
    `bench()` call; a non-movable local is fine.
- Wired into `aleph.cpu.cppm` via `export import :perf;` and added to the module's CMake
  `FILE_SET CXX_MODULES`.

### 4.2 `bench_harness.hpp`

`bench<F>(name, f, n_samples, iters)` keeps its shape (warm-up, N samples, sorted median,
`delta / iters`, escaped sink). The only change: a local `aleph::cpu::CycleCounter ctr;` per
`bench()` call, with `ctr.start()` before the measured region and `ctr.stop()` after,
replacing the `rdtscp()` reads. Reported `cyc/op` is now real core cycles.

### 4.3 `bench_main.cpp` — throughput restructuring

Break the loop-carried dependency in the four compute benches using **K = 8 independent,
in-register accumulators / inputs**, so consecutive iterations are independent and the loop
measures sustained throughput while staying compute-bound (no added load/store traffic).
The existing escaped sink is extended to keep all K live values observable so the optimizer
cannot elide them.

| Bench | Current (latency) | New (throughput) |
|---|---|---|
| Rotor compose | `a = a*b` | K independent `r[k] = a[k] * b` |
| Vec3 dot | `s += dot(a,b); a.x = s*1e-6` | K independent partial sums of `dot(a[k], b[k])` |
| Vec3 add | `a = a+b` | K independent `acc[k] = acc[k] + b` |
| Mat4 × Vec4 | `v = M*v` | K independent `out[k] = M * v[k]` |
| Arena allocate(64) | already independent allocations | unchanged (re-measured) |
| MpmcRing push+pop | already independent | unchanged (re-measured) |

The three informational ray benches (`hit_sphere`, `hit_quad`, BVH traversal) are already
independent-op loops with a sink; they are re-measured under perf, unchanged structurally.

The "K independent values, kept compute-bound, escaped sink" idiom is applied uniformly so
the four rewrites read consistently.

### 4.4 `run-baselines.sh`

- Run the benchmark binary under `taskset -c 2` (a P-core) so cycle counts are not perturbed
  by P↔E core migration on the hybrid CPU. The default `BIN` becomes
  `taskset -c 2 ./build-bench/bench/aleph_bench` (still overridable via `BIN=`).
- Add a header comment documenting the measurement basis: perf_event `CPU_CLK_UNHALTED`,
  throughput, P-core pinned, 155H.
- The existing per-bench target comparison and `exit "$fail"` logic is correct (verified:
  real exit code is 1 on regression) and is kept.

## 5. Recalibration (empirical, performed during implementation)

After the harness and loops are in place, run the bench pinned and quiet, then set each of the
six `TARGETS` by this rule:

- If the measured true-cycle throughput meets the **original** target (Rotor 6, Vec3 dot 3,
  Vec3 add 3, Mat4×Vec4 8, Arena 3, MPMC 60), **keep the original** — the code really is that
  fast and only the old harness was wrong.
- Otherwise set `target = ceil(measured × 1.15)` (≈15 % headroom for run-to-run drift) and
  record the measured value in a comment.

The point is an honest, reproducible gate — not preserving the old aspirational numbers.

## 6. Testing

- **TDD unit test** (`tests/cpu/test_cpu.cpp`): a `CycleCounter` exercise — count cycles around
  a non-trivial busy loop; assert the result is non-zero and within a sane order of magnitude
  (e.g. `> 0` and `< 10 × iterations`). Runs under the `release` preset in `ctest`
  (perf_event works there — `paranoid = 2`).
- **Integration:** `aleph_bench` builds under the `bench` preset, runs, and prints all rows;
  `run-baselines.sh` exits 0 with the recalibrated targets.
- **Regression:** re-run the full validation pass (strict/release/asan/bench builds + ctest +
  app smokes + baselines) and confirm everything stays green and the baseline gate is now
  meaningful.

## 7. Success criteria

1. `aleph.cpu:perf::CycleCounter` exists, is exported, and has a passing unit test.
2. `bench_harness.hpp` measures core cycles via perf_event; no `rdtscp` in the bench path.
3. The four compute benches measure throughput (independent-op loops).
4. `run-baselines.sh` pins to a P-core and exits 0 against recalibrated, documented targets.
5. Numbers are stable (<5 % run-to-run, pinned + quiet) and reproducible.
6. The full validation pass is green.

## 8. Risks & notes

- `exclude_kernel=1` measures user-space compute only (excludes syscall/interrupt cycles) — the
  intended quantity for these kernels.
- A single, non-multiplexed counter needs no `time_enabled/time_running` scaling.
- Context-switch noise during a 100 k-iteration sample is absorbed by pinning + short samples +
  median-of-50.
- Throughput numbers may move in **either** direction versus the old TSC figures; the
  recalibration in §5 is empirical and is the last implementation step.
