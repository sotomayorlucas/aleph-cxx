---
name: aleph-cxx-foundation-design
date: 2026-05-26
status: approved (sections 1-4 confirmed in brainstorming)
phase: 1 of 5
---

# aleph-cxx — Foundation (Phase 1)

## Context

`aleph-cxx` is a new, unified C++26 engine that fuses the DNA of four prior
projects:

| Source                          | LOC                | Contribution                                              |
| ---                             | ---                | ---                                                        |
| `/home/lkz/Sotark/` (cxx26)     | 3 978 C++26        | CPU renderers (raytracer + sw_renderer + editor), proven cycle-tight patterns |
| `/home/lkz/aleph-engine/`       | 25 751 Rust        | DPO graph rewrites + sheaf cohomology + DEC + Laplacian + TLA+/Coq formal heritage |
| `/home/lkz/aleph/` (legacy)     | 12 964 Rust        | BSP / Quake heritage + planner/replay/genome (ML/search) + scene types |
| `aleph-render-gpu` (stub)       | tiny               | (placeholder for future GPU port)                          |

**Total source DNA:** ≈ 42 700 LOC representing years of distinct R&D across
three paradigms (Quake-era cycle-tight, math-research, real-time GPU).

**Unifying goal:** *exprimir cada ciclo de reloj posible* — a CPU-tight
engine that leverages spatial algebra, differential geometry, and advanced
math, prioritising raw throughput over photoreal definition.

## Scope decomposition (5 phases)

| Phase | Topic                | LOC target | Builds on  |
| ---   | ---                  | ---        | ---        |
| **1** | **foundation**       | ~5 K       | nothing    |
| 2     | scene & render       | ~5 K       | foundation |
| 3     | DPO + topology       | ~4 K       | 1, 2       |
| 4     | DEC + flow           | ~3 K       | 1, 3       |
| 5     | GPU offload          | ~2 K       | 2, 4       |

**This spec covers Phase 1 only.** Each later phase will get its own
spec → plan → implementation cycle.

## Constraints (decided in brainstorming)

| Axis              | Decision                                                              |
| ---               | ---                                                                   |
| Location          | new repo `/home/lkz/aleph-cxx/`, branch `main`                        |
| Toolchain         | GCC 16.1.1, `-std=c++26`, C++20+ modules (P1689 dep scan)            |
| Build             | CMake 4.3 + Ninja                                                     |
| Test              | doctest (vendored single-header, reused from Sotark cxx26)           |
| SIMD              | AVX2 intrinsics for hot kernels + `std::simd` wrapper for general math; target `x86-64-v3` strict |
| Math depth        | Full kit — Vec/Mat/Quat/Rotor (GA G(3,0,0)) + dual numbers + tangent vectors |
| Data layout       | AoS API + SoA stores where massive sweep matters                      |
| Determinism       | No `std::unordered_*` in hot paths. `DenseIndex<Tag, T>` replaces hash maps. |
| Exception policy  | `release-strict` preset: `-fno-exceptions -fno-rtti`. Errors via `std::expected`. |

## Architecture

### Repo layout

```
/home/lkz/aleph-cxx/
  CMakeLists.txt              top-level
  CMakePresets.json           release-strict, release, debug, asan, bench
  cmake/                       module helpers, ISA probe, compiler flags
  third_party/
    doctest.h                  vendored single-header (reused from Sotark cxx26)
  foundation/                  THIS phase
    CMakeLists.txt
    src/
      aleph.math/              vec, mat, quat, rotor, bivector, trivector,
                                multivector, dual, tangent, aabb, ray, plane
      aleph.alloc/             arena, frame, slab, freelist, pmr_adapter
      aleph.containers/        small_vector, flat_set, dense_index, span_extra
      aleph.threads/           pool, barrier, mpmc_ring, work_stealing_deque
      aleph.io/                mmap, ppm, obj, byteswap
      aleph.cpu/               cpuid, perf, prefetch, branch_hints
  tests/                       doctest cases per module
  bench/                       microbenchmarks (criterion-style)
  docs/superpowers/specs/      this spec + later phases
```

### Module dependency graph (no cycles)

```
              aleph.cpu                  (lowest — no aleph deps)
                  ↑
        ┌─────────┼─────────┬─────────┐
    aleph.math  aleph.alloc  aleph.io
        ↑          ↑           ↑
        └──────────┴───────────┴── aleph.containers
                          ↑
                     aleph.threads  (depends on alloc + containers)
```

**Rules:**

- `aleph.cpu` imports nothing from aleph (only `<immintrin.h>`, `<cpuid.h>`, `<chrono>`).
- `aleph.math` never touches alloc/threads/io.
- Tests per module run in isolation — a math test cannot import threads.
- Cycle-deps prevented by CMake target deps + a `tests/test_graph.cpp`
  case that compiles a minimal program per module to verify isolation.

### Naming convention

- Root namespace: `aleph::`
- Sub-namespace per module: `aleph::math::Vec3`, `aleph::alloc::Arena`
- Types: `PascalCase`
- Functions: `snake_case`
- Concepts: `PascalCase`
- Macros: `ALEPH_*` only when no `[[gnu::*]]` or `constexpr` alternative exists.

## Module: `aleph.math`

### Primitive types

| Type           | Storage                     | Alignment      | Concept                          | Notes                                  |
| ---            | ---                         | ---            | ---                              | ---                                    |
| `Vec2`         | 2× f32                      | natural (8 B)  | `Vector<2>`                      | for UVs                                |
| `Vec3`         | **4× f32** (w=0 padding)    | `alignas(16)`  | `Vector<3>`, `Grade<1>`          | 1 Vec3 = 1 `__m128`                    |
| `Vec4`         | 4× f32                      | `alignas(16)`  | `Vector<4>`                      | proj / homogeneous                     |
| `Mat3`         | 3× Vec3 (col-major, padded) | `alignas(16)`  | —                                | 48 B                                   |
| `Mat4`         | 4× Vec4 (col-major)         | `alignas(64)`  | —                                | 1 cache line                           |
| `Quat`         | 4× f32                      | `alignas(16)`  | `Rotation`                       | LEGACY — interop with external assets only |
| **`Rotor`**    | **4× f32** (s + e₁₂ + e₂₃ + e₃₁) | `alignas(16)` | `Rotation`, `Multivector<{0,2}>` | **PRIMARY rotation type**              |
| `Bivector`     | 3× f32                      | natural        | `Grade<2>`                       | e₁₂, e₂₃, e₃₁                          |
| `Trivector`    | 1× f32                      | natural        | `Grade<3>`, `Pseudoscalar`       |                                        |
| `Multivector`  | 8× f32                      | `alignas(32)`  | union of all grades              | only where general product needed      |
| `Dual<T>`      | 2× T (val, eps)             | match T        | —                                | autodiff forward-mode                  |
| `TangentVec3`  | Vec3 + manifold tag         | `alignas(16)`  | tangent space                    | tag-distinct from Vec3                 |
| `Aabb`         | 2× Vec3                     | `alignas(16)`  | —                                | min/max                                |
| `Ray`          | 2× Vec3                     | `alignas(16)`  | —                                | origin/dir                             |
| `Plane`        | Vec3 + f32                  | `alignas(16)`  | —                                | Hessian normal form                    |

### Geometric Algebra G(3,0,0)

Basis: `{1, e₁, e₂, e₃, e₁₂, e₂₃, e₃₁, e₁₂₃}` — 8 dim. `Multivector` stores all 8 densely.

**Operations:**

```cpp
namespace aleph::math {
  Rotor       operator*(Rotor a, Rotor b)         noexcept;   // 8 mul + 7 add
  Multivector operator*(Vec3 a, Vec3 b)           noexcept;   // a·b + a∧b
  Bivector    wedge(Vec3 a, Vec3 b)               noexcept;
  Vec3        apply(Rotor R, Vec3 v)              noexcept;   // R v R⁻¹
  Rotor       from_axis_angle(Vec3 axis, f32 rad) noexcept;
  Rotor       slerp(Rotor a, Rotor b, f32 t)      noexcept;
  Rotor       reverse(Rotor R)                    noexcept;   // R†
  Mat3        to_mat3(Rotor R)                    noexcept;
  Rotor       from_quat(Quat q)                   noexcept;
}
```

**Static-asserted algebraic laws** (compile-time, zero runtime cost):

```cpp
static_assert(std::is_trivially_copyable_v<Rotor>);
static_assert(sizeof(Rotor) == 16);
constexpr Rotor a = from_axis_angle({1,0,0}, 0.5f);
constexpr Rotor b = from_axis_angle({0,1,0}, 0.3f);
constexpr Rotor c = from_axis_angle({0,0,1}, 0.7f);
static_assert(approx_eq(a * (b * c), (a * b) * c, 1e-6f));   // associativity
static_assert(approx_eq(reverse(a) * a, Rotor::identity(), 1e-6f));   // inverse
```

### Dual numbers (autodiff)

```cpp
template<typename T> struct Dual { T val; T eps; };
Dual<f32>  sin(Dual<f32> x);
Dual<Vec3> normalize(Dual<Vec3> v);
```

Generic over `T`: `Dual<f32>` for scalars, `Dual<Vec3>` for vector fields,
`Dual<Rotor>` for differentiable rotations. **Anticipated use:** BRDF
parameter gradients (Phase 4 path guiding), sheaf morphism Jacobians.

### Tangent vectors

```cpp
template<typename Manifold> struct TangentVec { Vec3 v; };
using TangentR3      = TangentVec<struct R3>;
using TangentSurface = TangentVec<struct Surf>;
```

Tag-distinct from `Vec3` — prevents accidental mixing of world / tangent /
object coordinates, the #1 cause of visual bugs in BRDF / normal mapping.

### SIMD strategy

- Single-vector ops (add/sub/dot/normalize): `__m128` directly.
- Batch ops (rays vs sphere arrays, transform N vertices): functions in
  `aleph::math::batch::` take `std::span<Vec3>` / SoA arrays, process
  8 lanes with `__m256`.
- Rotor compose: 8 mul + 7 add with FMA (`_mm_fmadd_ps`).

## Module: `aleph.alloc`

| Resource        | Use                                                         | Free policy                          | Thread-safety              |
| ---             | ---                                                         | ---                                  | ---                        |
| **`Arena`**     | scratch single-shot (BVH build, OBJ parse)                  | bump pointer + bulk `reset()`        | single-thread (1 per worker) |
| **`Frame`**     | renderer scratch freed at end-of-frame                      | bump + double-buffered `release_frame()` | single-thread per stripe |
| **`Slab<N>`**   | fixed-size churn (BVH nodes, ray packets, materials)        | intrusive free-list in free blocks   | optional via tag           |
| **`FreeList`**  | mixed-size with size-segregated lists (8/16/32/64/128/256 B) | size-segregated free lists           | optional atomic CAS         |

**Rules:**

- All resources `noexcept`. Allocation failure returns `nullptr`.
- Alignment configurable up to 64 B (cache line). Header injected when align > 16.
- Debug-only stats: `bytes_allocated()`, `bytes_in_use()`, `peak_in_use()`,
  `n_allocations()` — compiled to noop in release.
- `pmr_adapter<T>` makes any resource usable with `std::pmr::vector<T>` etc.

## Module: `aleph.containers`

```cpp
namespace aleph::containers {
  template<typename T, size_t N>      class SmallVector;
  template<typename K, typename Cmp>  class FlatSet;
  template<typename Tag, typename T>  class DenseIndex;  // type-safe IDs

  template<typename T> using SpanMut   = std::span<T>;
  template<typename T> using SpanConst = std::span<const T>;
  // mdspan helpers for row/col-major + stride layouts.
}
```

- `SmallVector<T,N>` — inline storage, heap fallback via pmr.
- `FlatSet` — sorted vector + binary search (faster than `set` for N < ~64).
- `DenseIndex<Tag,T>` — `std::vector<T>` + typed `Handle<Tag>(u32)`. O(1)
  lookup, deterministic iteration. **Replaces `IndexMap` of aleph-engine.**

**No hash maps in hot paths.** Determinism is load-bearing for testing,
golden snapshots, and (later) Mayer-Vietoris rewrites.

## Module: `aleph.threads`

```cpp
namespace aleph::threads {
  class Pool {
   public:
    explicit Pool(int n_threads);
    template<typename F> auto submit(F&& f) -> std::future<…>;
    void parallel_for(int begin, int end, std::invocable<int> auto f);
    void wait_idle();
  };

  using Barrier = std::barrier<>;
  template<typename T, size_t N> class MpmcRing;        // Vyukov bounded MPMC
  template<typename T>           class WorkStealingDeque;  // Chase-Lev
}
```

**Rules:**

- Persistent worker pool (no spawn-per-task).
- `parallel_for` uses dynamic chunks (`std::atomic<int>` counter).
- `WorkStealingDeque` for recursive subtree work (BVH build).
- `MpmcRing` bounded — no lock-free + alloc combination.
- No fibers, no coroutines in pool. Coroutines OK in I/O pipelines.

## Module: `aleph.io`

```cpp
namespace aleph::io {
  class MappedFile {
   public:
    static std::expected<MappedFile, std::string> open_read(std::string_view);
    std::span<const std::byte> bytes() const noexcept;
  };

  std::expected<Image,   std::string> load_ppm(std::span<const std::byte>);
  std::expected<ObjMesh, std::string> load_obj(std::span<const std::byte>);

  constexpr u32 byteswap32(u32 v) noexcept;   // delegates to std::byteswap
}
```

mmap + span instead of `FILE*` + `fread`. Parsers consume pages directly —
zero copy. RAII handles `munmap`.

## Module: `aleph.cpu`

```cpp
namespace aleph::cpu {
  inline constexpr bool has_avx2     = true;   // required by build flag
  inline constexpr bool has_fma      = true;
  inline constexpr bool has_avx_vnni = false;
  inline constexpr int  cache_line   = 64;

  void assert_isa_compatible();   // [[noreturn]] on failure (runtime CPUID)

  template<typename T> [[gnu::always_inline]] void prefetch(const T*) noexcept;
  inline void compiler_barrier() noexcept { asm volatile("" ::: "memory"); }

  #define ALEPH_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define ALEPH_UNLIKELY(x) __builtin_expect(!!(x), 0)

  u64 rdtsc()  noexcept;
  u64 rdtscp() noexcept;
}
```

`assert_isa_compatible()` runs in `main()`. Aborts if AVX2/FMA/BMI2 missing.

## Build / flags

**`release-strict` preset (the target — ship build):**

```
-O3 -DNDEBUG
-march=x86-64-v3 -mavx2 -mfma -mbmi2
-fno-rtti
-fno-exceptions
-fno-plt -fno-semantic-interposition
-fno-stack-protector
-fvisibility=hidden -fvisibility-inlines-hidden
-funroll-loops -fpeel-loops -ftree-vectorize
-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
-Wformat=2 -Wmissing-declarations -Wpadded -Wsign-conversion
```

| Preset           | Build type      | Notes                                         |
| ---              | ---             | ---                                           |
| `release-strict` | Release         | flags above — the target                       |
| `release`        | Release         | without `-fno-exceptions/-fno-rtti` (for tests) |
| `debug`          | Debug           | `-O0 -g3`, invariants on                       |
| `asan`           | RelWithDebInfo  | `-fsanitize=address,undefined`                 |
| `bench`          | Release         | adds bench-specific opt control                |

## Testing

```
tests/
  test_main.cpp                doctest entry
  math/test_{vec,mat,rotor,dual,tangent,aabb_ray_plane}.cpp
  alloc/test_{arena,frame,slab,freelist,pmr_adapter}.cpp
  containers/test_{small_vector,flat_set,dense_index}.cpp
  threads/test_{pool,mpmc,work_stealing}.cpp
  io/test_{mmap,ppm,obj}.cpp
  cpu/test_cpuid.cpp
```

doctest compiles under the `release` preset (needs exceptions).
`release-strict` excludes tests.

## Verification posture

| Layer              | Mechanism                                              | Runtime cost  |
| ---                | ---                                                     | ---           |
| Algebraic laws     | `static_assert(approx_eq(a*(b*c), (a*b)*c))`            | 0             |
| Type invariants    | concepts (`Vector<T>`, `Rotation<T>`, `Allocator<T>`)   | 0             |
| Layout invariants  | `static_assert(sizeof(T) == N && alignof(T) == A)`      | 0             |
| Logic invariants   | `assert(...)` (NDEBUG = noop)                           | 0 release     |
| Memory safety      | `asan` preset in CI                                     | 0 release     |
| Property tests     | doctest `SUBCASE` + Pcg32 seeded random                 | 0 release     |
| **Out of scope**   | Coq, TLA+ (deferred — possibly Phase 4 if needed)       | —             |

## Microbenchmarks (`bench/`)

Tiny home-grown harness (no Google Benchmark dep) using `aleph::cpu::rdtscp()`.

**Day-1 baselines to measure:**

| Op                                | Target          |
| ---                               | ---             |
| `Rotor compose`                   | ≤ 6 cycles      |
| `Vec3 add / dot`                  | ≤ 3 cycles      |
| `Mat4 * Vec4`                     | ≤ 8 cycles      |
| `Arena::allocate(64)`             | ≤ 3 cycles      |
| `Pool::parallel_for` overhead     | ≤ 200 ns        |
| `MpmcRing<u64,1024>` push/pop     | ≤ 10 ns uncontended |

## CI / pre-commit

Local hooks for now:
`cmake --build build-release-strict && ctest && bench/run-baselines.sh`.
Remote CI when repo is published.

## Out of scope for Phase 1

- Scene graph (Phase 2)
- DPO rewrite engine (Phase 3)
- Sheaf cohomology / DEC (Phase 4)
- GPU / CUDA (Phase 5)
- Coq / TLA+ formal verification (deferred)
- Single-header amalgamation (defer until external users exist)
- Cross-platform build (Linux + GCC 16 only initially; Clang and macOS
  decided based on actual user need)

## Success criteria for Phase 1

1. All modules compile under `release-strict` preset with zero warnings.
2. All tests pass under `release` preset (target: ≥ 200 doctest cases).
3. All baseline benchmarks hit their cycle targets on Core Ultra 7 155H.
4. `aleph.cpu::assert_isa_compatible()` aborts cleanly on simulated
   non-AVX2 host (CI uses `qemu -cpu nehalem` to verify).
5. asan + ubsan run clean against the full test suite.
6. Module dependency graph has no cycles (enforced by an isolation test).
