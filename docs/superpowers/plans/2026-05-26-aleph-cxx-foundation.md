# aleph-cxx Foundation (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the foundation layer of `aleph-cxx`: 6 C++26 modules (`math`, `alloc`, `containers`, `threads`, `io`, `cpu`) totalling ~5 K LOC, with strict AVX2 baseline, GA G(3,0,0) rotors, custom pmr allocators, lock-free concurrency primitives, doctest test suite, and microbench harness hitting cycle-target baselines on Intel Core Ultra 7 155H.

**Architecture:** Six self-contained C++20+ module libraries with a strict dependency DAG (`cpu` → {`math`, `alloc`, `io`} → `containers` → `threads`). Tests live in a separate target compiled under the `release` preset; the `release-strict` preset is `-fno-exceptions -fno-rtti` and excludes tests. Each module is one CMake `add_library` with `FILE_SET CXX_MODULES` partitioned by responsibility.

**Tech Stack:** GCC 16.1.1 `-std=c++26`, CMake 4.3 + Ninja, doctest 2.4.11 (vendored header), AVX2 intrinsics (`<immintrin.h>`), `std::simd`, `std::pmr`, `std::jthread`, `std::barrier`, `std::expected`, `std::mdspan`, `std::print`.

---

## Task 1: Repo skeleton + top-level CMake + first compile

**Files:**
- Create: `/home/lkz/aleph-cxx/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/CMakePresets.json`
- Create: `/home/lkz/aleph-cxx/cmake/aleph_flags.cmake`
- Create: `/home/lkz/aleph-cxx/foundation/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/.gitignore`

- [ ] **Step 1.1: Write `.gitignore`**

```
build*/
compile_commands.json
*.gcm
*.cppm.o.modmap
.cache/
.vscode/
.idea/
```

- [ ] **Step 1.2: Write top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.28)
project(aleph_cxx
    VERSION 0.1.0
    DESCRIPTION "aleph-cxx — cycle-tight C++26 engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/aleph_flags.cmake)

add_subdirectory(foundation)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 1.3: Write `cmake/aleph_flags.cmake`**

```cmake
# INTERFACE library carrying common compile options. Each aleph target
# links to one of these depending on its purpose.

add_library(aleph_flags_common INTERFACE)
target_compile_options(aleph_flags_common INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
    -Wformat=2 -Wmissing-declarations -Wsign-conversion
    -fdiagnostics-color=always)

add_library(aleph_flags_strict INTERFACE)
target_compile_options(aleph_flags_strict INTERFACE
    -march=x86-64-v3 -mavx2 -mfma -mbmi2
    -fno-rtti -fno-exceptions
    -fno-plt -fno-semantic-interposition
    -fno-stack-protector
    -fvisibility=hidden -fvisibility-inlines-hidden
    -funroll-loops -fpeel-loops -ftree-vectorize)
target_link_libraries(aleph_flags_strict INTERFACE aleph_flags_common)

add_library(aleph_flags_test INTERFACE)
# tests need exceptions for doctest; keep ISA strict so SIMD code paths
# behave identically to production.
target_compile_options(aleph_flags_test INTERFACE
    -march=x86-64-v3 -mavx2 -mfma -mbmi2)
target_link_libraries(aleph_flags_test INTERFACE aleph_flags_common)
```

- [ ] **Step 1.4: Write `CMakePresets.json`**

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "release-strict",
      "displayName": "Release (cycle-tight ship build)",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-release-strict",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS_RELEASE": "-O3 -DNDEBUG"
      }
    },
    {
      "name": "release",
      "displayName": "Release (with tests)",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS_RELEASE": "-O3 -DNDEBUG"
      }
    },
    {
      "name": "debug",
      "displayName": "Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_FLAGS_DEBUG": "-O0 -g3 -fno-omit-frame-pointer"
      }
    },
    {
      "name": "asan",
      "displayName": "ASan + UBSan",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-asan",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_CXX_FLAGS_RELWITHDEBINFO":
          "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
      }
    },
    {
      "name": "bench",
      "displayName": "Bench",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-bench",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS_RELEASE":
          "-O3 -DNDEBUG -fno-inline-functions-called-once"
      }
    }
  ],
  "buildPresets": [
    { "name": "release-strict", "configurePreset": "release-strict" },
    { "name": "release",        "configurePreset": "release" },
    { "name": "debug",          "configurePreset": "debug" },
    { "name": "asan",           "configurePreset": "asan" },
    { "name": "bench",          "configurePreset": "bench" }
  ]
}
```

- [ ] **Step 1.5: Write empty `foundation/CMakeLists.txt` and `tests/CMakeLists.txt` placeholders**

`foundation/CMakeLists.txt`:
```cmake
# Modules added by later tasks via add_subdirectory.
add_subdirectory(src/aleph.cpu)
```

`tests/CMakeLists.txt`:
```cmake
# Tests added by later tasks.
```

Create directory `foundation/src/aleph.cpu/` (empty for now, populated in Task 3).

- [ ] **Step 1.6: Configure and verify CMake setup**

Run: `cd /home/lkz/aleph-cxx && cmake --preset release-strict 2>&1 | tail -15`
Expected: `-- Configuring done` and `-- Generating done`, no errors.

- [ ] **Step 1.7: Commit**

```bash
cd /home/lkz/aleph-cxx
git add .gitignore CMakeLists.txt CMakePresets.json cmake/ foundation/CMakeLists.txt tests/CMakeLists.txt
mkdir -p foundation/src/aleph.cpu && git add -A foundation/src/aleph.cpu  # tracks empty dir via .gitkeep if needed
git commit -m "task 1: repo skeleton + CMake presets + flags"
```

---

## Task 2: doctest vendoring + test harness

**Files:**
- Create: `/home/lkz/aleph-cxx/third_party/doctest.h` (vendored from Sotark cxx26)
- Create: `/home/lkz/aleph-cxx/tests/test_main.cpp`
- Create: `/home/lkz/aleph-cxx/tests/test_smoke.cpp`
- Modify: `/home/lkz/aleph-cxx/tests/CMakeLists.txt`

- [ ] **Step 2.1: Vendor doctest**

```bash
mkdir -p /home/lkz/aleph-cxx/third_party
cp /home/lkz/Sotark/third_party/doctest.h /home/lkz/aleph-cxx/third_party/doctest.h
```

Verify: `wc -l /home/lkz/aleph-cxx/third_party/doctest.h` should print `7106 …/doctest.h`.

- [ ] **Step 2.2: Write `tests/test_main.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 2.3: Write a smoke test that intentionally fails first**

`tests/test_smoke.cpp`:
```cpp
#include "doctest.h"

TEST_CASE("smoke: doctest itself works") {
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 2.4: Update `tests/CMakeLists.txt`**

```cmake
add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE aleph_flags_test)
add_test(NAME aleph_tests COMMAND aleph_tests)
```

- [ ] **Step 2.5: Build + run tests under `release` preset**

```bash
cd /home/lkz/aleph-cxx
cmake --preset release
cmake --build build-release --target aleph_tests
ctest --test-dir build-release --output-on-failure
```

Expected: `1/1 Test #1: aleph_tests ........ Passed`.

- [ ] **Step 2.6: Commit**

```bash
git add third_party/doctest.h tests/test_main.cpp tests/test_smoke.cpp tests/CMakeLists.txt
git commit -m "task 2: doctest vendored, test harness wired, smoke test green"
```

---

## Task 3: `aleph.cpu` module — CPUID + perf counters + hints

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.cpu/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.cpu/aleph.cpu.cppm`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.cpu/aleph.cpu-isa.cppm`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.cpu/aleph.cpu-cycles.cppm`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.cpu/aleph.cpu-hints.cppm`
- Create: `/home/lkz/aleph-cxx/tests/cpu/test_cpu.cpp`
- Modify: `/home/lkz/aleph-cxx/tests/CMakeLists.txt`

- [ ] **Step 3.1: Write failing test `tests/cpu/test_cpu.cpp`**

```cpp
#include "doctest.h"
import aleph.cpu;

using namespace aleph::cpu;

TEST_CASE("cpu constants reflect build flags") {
    static_assert(has_avx2);
    static_assert(has_fma);
    static_assert(cache_line == 64);
    CHECK(has_avx2);
    CHECK(has_fma);
}

TEST_CASE("assert_isa_compatible does not abort on AVX2 host") {
    assert_isa_compatible();   // would abort if AVX2 missing
    CHECK(true);
}

TEST_CASE("rdtsc / rdtscp are monotonic across consecutive calls") {
    const auto a = rdtsc();
    for (int i = 0; i < 1000; ++i) {
        asm volatile("" ::: "memory");  // discourage hoisting
    }
    const auto b = rdtsc();
    CHECK(b > a);

    const auto c = rdtscp();
    for (int i = 0; i < 1000; ++i) asm volatile("" ::: "memory");
    const auto d = rdtscp();
    CHECK(d > c);
}

TEST_CASE("prefetch is a no-op effect-wise (compile + run smoke)") {
    int data[64]{};
    prefetch(data);            // shouldn't crash
    prefetch(data + 32);
    CHECK(true);
}
```

- [ ] **Step 3.2: Run test to verify it fails (module doesn't exist yet)**

Update `tests/CMakeLists.txt` to add the new file:
```cmake
add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp
    cpu/test_cpu.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE aleph_flags_test aleph_cpu)
add_test(NAME aleph_tests COMMAND aleph_tests)
```

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -10`
Expected: error mentioning `aleph.cpu` or `aleph_cpu` not found.

- [ ] **Step 3.3: Write `foundation/src/aleph.cpu/aleph.cpu-isa.cppm`**

```cpp
module;
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cpuid.h>

export module aleph.cpu:isa;

export namespace aleph::cpu {

inline constexpr bool has_avx2     = true;   // required at build time
inline constexpr bool has_fma      = true;
inline constexpr bool has_bmi2     = true;
inline constexpr bool has_avx_vnni = false;  // optional fast path probe
inline constexpr int  cache_line   = 64;

// Runtime CPUID probe. Aborts with a clear message if any required ISA
// feature is missing on the host CPU. Should be called once from main().
[[gnu::cold]] inline void assert_isa_compatible() {
    unsigned a, b, c, d;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d) == 0) {
        std::fprintf(stderr, "aleph.cpu: CPUID leaf 7 unavailable — abort.\n");
        std::abort();
    }
    const bool runtime_avx2 = (b >> 5) & 1u;
    const bool runtime_bmi2 = (b >> 8) & 1u;
    unsigned a1, b1, c1, d1;
    __get_cpuid(1, &a1, &b1, &c1, &d1);
    const bool runtime_fma = (c1 >> 12) & 1u;
    if (!runtime_avx2 || !runtime_fma || !runtime_bmi2) {
        std::fprintf(stderr,
            "aleph.cpu: required ISA features missing on host "
            "(avx2=%d fma=%d bmi2=%d). Rebuild for an older baseline.\n",
            runtime_avx2, runtime_fma, runtime_bmi2);
        std::abort();
    }
}

}  // namespace aleph::cpu
```

- [ ] **Step 3.4: Write `foundation/src/aleph.cpu/aleph.cpu-cycles.cppm`**

```cpp
module;
#include <cstdint>
#include <x86intrin.h>

export module aleph.cpu:cycles;

export namespace aleph::cpu {

// Read time-stamp counter. NOT serializing — out-of-order CPUs may
// reorder reads around it. Use rdtscp() when you need a fence.
[[gnu::always_inline]] inline std::uint64_t rdtsc() noexcept {
    return __rdtsc();
}

// Serializing variant: prevents earlier instructions from being moved
// after the read. Use to time short kernels.
[[gnu::always_inline]] inline std::uint64_t rdtscp() noexcept {
    unsigned aux;
    return __rdtscp(&aux);
}

[[gnu::always_inline]] inline void compiler_barrier() noexcept {
    asm volatile("" ::: "memory");
}

}  // namespace aleph::cpu
```

- [ ] **Step 3.5: Write `foundation/src/aleph.cpu/aleph.cpu-hints.cppm`**

```cpp
module;
#include <xmmintrin.h>

export module aleph.cpu:hints;

export namespace aleph::cpu {

// Software prefetch into L1. Use 1 cache line ahead in tight inner loops.
template<typename T>
[[gnu::always_inline]] inline void prefetch(const T* p) noexcept {
    __builtin_prefetch(p, /*rw=*/0, /*locality=*/3);
}

template<typename T>
[[gnu::always_inline]] inline void prefetch_write(T* p) noexcept {
    __builtin_prefetch(p, /*rw=*/1, /*locality=*/3);
}

}  // namespace aleph::cpu

// Branch hints. Macros only because __builtin_expect needs to wrap a
// conditional expression directly.
#define ALEPH_LIKELY(x)   __builtin_expect(!!(x), 1)
#define ALEPH_UNLIKELY(x) __builtin_expect(!!(x), 0)
```

- [ ] **Step 3.6: Write primary module unit `aleph.cpu.cppm`**

```cpp
export module aleph.cpu;
export import :isa;
export import :cycles;
export import :hints;
```

- [ ] **Step 3.7: Write `foundation/src/aleph.cpu/CMakeLists.txt`**

```cmake
add_library(aleph_cpu)
target_sources(aleph_cpu
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.cpu.cppm
        aleph.cpu-isa.cppm
        aleph.cpu-cycles.cppm
        aleph.cpu-hints.cppm)
target_link_libraries(aleph_cpu PRIVATE aleph_flags_strict)
```

- [ ] **Step 3.8: Create `tests/cpu/` and update `tests/CMakeLists.txt`** (already done in 3.2). Configure with the test target picking up the new files:

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -15`
Expected: clean build, no errors.

- [ ] **Step 3.9: Run tests**

```bash
./build-release/tests/aleph_tests --test-case-exclude="" 2>&1 | tail -6
```
Expected: `assertions: N | N passed | 0 failed`.

- [ ] **Step 3.10: Commit**

```bash
git add foundation/src/aleph.cpu/ tests/cpu/test_cpu.cpp tests/CMakeLists.txt
git commit -m "task 3: aleph.cpu — CPUID assert, rdtsc/rdtscp, prefetch + branch hints"
```

---

## Task 4: `aleph.math:types` — scalar aliases + concepts

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-types.cppm`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-concepts.cppm`
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_types.cpp`
- Modify: `/home/lkz/aleph-cxx/foundation/CMakeLists.txt` → add `add_subdirectory(src/aleph.math)`
- Modify: `/home/lkz/aleph-cxx/tests/CMakeLists.txt`

- [ ] **Step 4.1: Write failing test**

`tests/math/test_types.cpp`:
```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("scalar aliases have the expected widths") {
    static_assert(sizeof(u8)  == 1);
    static_assert(sizeof(u16) == 2);
    static_assert(sizeof(u32) == 4);
    static_assert(sizeof(u64) == 8);
    static_assert(sizeof(i32) == 4);
    static_assert(sizeof(f32) == 4);
    static_assert(sizeof(f64) == 8);
    CHECK(true);
}

TEST_CASE("approx_eq scalar") {
    CHECK(approx_eq(1.0f, 1.0f, 0.0f));
    CHECK(approx_eq(1.0f, 1.0f + 1e-9f, 1e-6f));
    CHECK_FALSE(approx_eq(1.0f, 1.1f, 1e-6f));
}
```

- [ ] **Step 4.2: Run to see fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: error about `aleph.math` not found.

- [ ] **Step 4.3: Write `aleph.math-types.cppm`**

```cpp
module;
#include <cstdint>
#include <cstddef>
#include <cmath>

export module aleph.math:types;

export namespace aleph::math {

using u8    = std::uint8_t;
using u16   = std::uint16_t;
using u32   = std::uint32_t;
using u64   = std::uint64_t;
using i8    = std::int8_t;
using i16   = std::int16_t;
using i32   = std::int32_t;
using i64   = std::int64_t;
using f32   = float;
using f64   = double;
using usize = std::size_t;

[[nodiscard]] constexpr bool approx_eq(f32 a, f32 b, f32 eps = 1e-6f) noexcept {
    const f32 d = a - b;
    return (d < 0.0f ? -d : d) <= eps;
}

[[nodiscard]] constexpr bool approx_eq(f64 a, f64 b, f64 eps = 1e-12) noexcept {
    const f64 d = a - b;
    return (d < 0.0 ? -d : d) <= eps;
}

}  // namespace aleph::math
```

- [ ] **Step 4.4: Write `aleph.math-concepts.cppm`**

```cpp
module;
#include <concepts>
#include <type_traits>

export module aleph.math:concepts;

import :types;

export namespace aleph::math {

// True iff T is a vector type with N components of the same scalar.
template<typename T, int N>
concept Vector = requires {
    typename T::scalar_type;
    requires std::is_trivially_copyable_v<T>;
    requires sizeof(T) >= sizeof(typename T::scalar_type) * N;
};

// True iff T behaves as a rotation: compose, identity, inverse exist.
template<typename T>
concept Rotation = requires(T a, T b) {
    { a * b } -> std::same_as<T>;
    { T::identity() } -> std::same_as<T>;
};

// Grade<k> marks elements of a specific G-algebra grade.
template<typename T, int K>
concept Grade = requires {
    requires T::grade == K;
};

}  // namespace aleph::math
```

- [ ] **Step 4.5: Write primary unit `aleph.math.cppm`**

```cpp
export module aleph.math;
export import :types;
export import :concepts;
```

- [ ] **Step 4.6: Write `foundation/src/aleph.math/CMakeLists.txt`**

```cmake
add_library(aleph_math)
target_sources(aleph_math
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.math.cppm
        aleph.math-types.cppm
        aleph.math-concepts.cppm)
target_link_libraries(aleph_math
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_strict)
```

- [ ] **Step 4.7: Wire foundation + tests**

`foundation/CMakeLists.txt`:
```cmake
add_subdirectory(src/aleph.cpu)
add_subdirectory(src/aleph.math)
```

`tests/CMakeLists.txt`:
```cmake
add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp
    cpu/test_cpu.cpp
    math/test_types.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math)
add_test(NAME aleph_tests COMMAND aleph_tests)
```

- [ ] **Step 4.8: Build + test**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
```
Expected: all tests pass.

- [ ] **Step 4.9: Commit**

```bash
git add foundation/src/aleph.math/ foundation/CMakeLists.txt tests/math/test_types.cpp tests/CMakeLists.txt
git commit -m "task 4: aleph.math:types + :concepts — scalar aliases, approx_eq, Vector/Rotation/Grade"
```

---

## Task 5: `aleph.math:vec` — Vec2/Vec3/Vec4 + AVX2 ops

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-vec.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_vec.cpp`
- Modify: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math.cppm`
- Modify: `/home/lkz/aleph-cxx/foundation/src/aleph.math/CMakeLists.txt`
- Modify: `/home/lkz/aleph-cxx/tests/CMakeLists.txt`

- [ ] **Step 5.1: Write failing test**

`tests/math/test_vec.cpp`:
```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Vec3 layout: 16-byte aligned, padded w=0") {
    static_assert(sizeof(Vec3)  == 16);
    static_assert(alignof(Vec3) == 16);
    Vec3 v{1, 2, 3};
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.0f);
    CHECK(v.z == 3.0f);
    CHECK(v.w == 0.0f);   // padding always zero
}

TEST_CASE("Vec3 arithmetic") {
    constexpr Vec3 a{1, 2, 3};
    constexpr Vec3 b{4, 5, 6};
    CHECK((a + b) == Vec3{5, 7, 9});
    CHECK((a - b) == Vec3{-3, -3, -3});
    CHECK((a * 2.0f) == Vec3{2, 4, 6});
    CHECK((-a) == Vec3{-1, -2, -3});
    CHECK(dot(a, b) == doctest::Approx(32.0f));
    CHECK(cross(a, b) == Vec3{-3, 6, -3});
    CHECK(length_sq(Vec3{3, 4, 0}) == doctest::Approx(25.0f));
}

TEST_CASE("Vec3 normalize + length") {
    const Vec3 n = normalize(Vec3{3, 4, 0});
    CHECK(length(n) == doctest::Approx(1.0f).epsilon(1e-6f));
    CHECK(n.x == doctest::Approx(0.6f));
    CHECK(n.y == doctest::Approx(0.8f));
}

TEST_CASE("Vec2 basics") {
    static_assert(sizeof(Vec2) == 8);
    constexpr Vec2 a{1, 2}, b{3, 4};
    CHECK((a + b) == Vec2{4, 6});
}

TEST_CASE("Vec4 basics") {
    static_assert(sizeof(Vec4) == 16);
    static_assert(alignof(Vec4) == 16);
    constexpr Vec4 a{1, 2, 3, 4}, b{5, 6, 7, 8};
    CHECK((a + b) == Vec4{6, 8, 10, 12});
}
```

- [ ] **Step 5.2: Run to see fail**

Run: `cmake --build build-release --target aleph_tests 2>&1 | tail -5`
Expected: error mentioning Vec2/Vec3/Vec4.

- [ ] **Step 5.3: Write `aleph.math-vec.cppm`**

```cpp
module;
#include <cmath>
#include <immintrin.h>

export module aleph.math:vec;

import :types;

export namespace aleph::math {

struct Vec2 {
    f32 x{}, y{};
    using scalar_type = f32;

    constexpr Vec2 operator+(Vec2 b) const noexcept { return {x + b.x, y + b.y}; }
    constexpr Vec2 operator-(Vec2 b) const noexcept { return {x - b.x, y - b.y}; }
    constexpr Vec2 operator*(f32 s)  const noexcept { return {x * s,   y * s}; }
    constexpr Vec2 operator-()       const noexcept { return {-x, -y}; }
    constexpr bool operator==(Vec2 o) const noexcept { return x == o.x && y == o.y; }
};

// 16-byte aligned, w padded to 0. 1 Vec3 == 1 __m128 — single-vector
// ops fold to one SIMD insn each.
struct alignas(16) Vec3 {
    f32 x{}, y{}, z{}, w{};
    using scalar_type = f32;
    static constexpr int grade = 1;

    constexpr Vec3() = default;
    constexpr Vec3(f32 X, f32 Y, f32 Z) noexcept : x{X}, y{Y}, z{Z}, w{0} {}

    constexpr Vec3 operator+(Vec3 b) const noexcept { return {x + b.x, y + b.y, z + b.z}; }
    constexpr Vec3 operator-(Vec3 b) const noexcept { return {x - b.x, y - b.y, z - b.z}; }
    constexpr Vec3 operator*(f32 s)  const noexcept { return {x * s,   y * s,   z * s}; }
    constexpr Vec3 operator*(Vec3 b) const noexcept { return {x * b.x, y * b.y, z * b.z}; }
    constexpr Vec3 operator/(f32 s)  const noexcept { return {x / s,   y / s,   z / s}; }
    constexpr Vec3 operator-()       const noexcept { return {-x, -y, -z}; }
    constexpr Vec3& operator+=(Vec3 b) noexcept { x += b.x; y += b.y; z += b.z; return *this; }
    constexpr Vec3& operator-=(Vec3 b) noexcept { x -= b.x; y -= b.y; z -= b.z; return *this; }
    constexpr Vec3& operator*=(f32 s)  noexcept { x *= s;   y *= s;   z *= s;   return *this; }
    constexpr bool  operator==(Vec3 o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct alignas(16) Vec4 {
    f32 x{}, y{}, z{}, w{};
    using scalar_type = f32;

    constexpr Vec4 operator+(Vec4 b) const noexcept {
        return {x + b.x, y + b.y, z + b.z, w + b.w};
    }
    constexpr Vec4 operator-(Vec4 b) const noexcept {
        return {x - b.x, y - b.y, z - b.z, w - b.w};
    }
    constexpr Vec4 operator*(f32 s) const noexcept {
        return {x * s, y * s, z * s, w * s};
    }
    constexpr bool operator==(Vec4 o) const noexcept {
        return x == o.x && y == o.y && z == o.z && w == o.w;
    }
};

[[nodiscard]] constexpr f32 dot(Vec3 a, Vec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}
[[nodiscard]] constexpr f32 length_sq(Vec3 a) noexcept { return dot(a, a); }

[[nodiscard]] inline f32 length(Vec3 a) noexcept { return std::sqrt(length_sq(a)); }

[[nodiscard]] inline Vec3 normalize(Vec3 a) noexcept {
    return a * (1.0f / length(a));
}

[[nodiscard]] constexpr Vec3 lerp(Vec3 a, Vec3 b, f32 t) noexcept {
    return a * (1.0f - t) + b * t;
}

[[nodiscard]] constexpr Vec3 reflect(Vec3 v, Vec3 n) noexcept {
    return v - n * (2.0f * dot(v, n));
}

[[nodiscard]] constexpr bool near_zero(Vec3 v) noexcept {
    constexpr f32 eps = 1e-8f;
    auto abs_f = [](f32 x) constexpr { return x < 0 ? -x : x; };
    return abs_f(v.x) < eps && abs_f(v.y) < eps && abs_f(v.z) < eps;
}

}  // namespace aleph::math
```

- [ ] **Step 5.4: Update primary unit + CMake + test list**

`aleph.math.cppm`:
```cpp
export module aleph.math;
export import :types;
export import :concepts;
export import :vec;
```

`foundation/src/aleph.math/CMakeLists.txt` (FILES list):
```cmake
target_sources(aleph_math
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.math.cppm
        aleph.math-types.cppm
        aleph.math-concepts.cppm
        aleph.math-vec.cppm)
```

Append `math/test_vec.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 5.5: Build + test**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
```
Expected: all tests pass.

- [ ] **Step 5.6: Commit**

```bash
git add foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/aleph.math-vec.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_vec.cpp tests/CMakeLists.txt
git commit -m "task 5: aleph.math:vec — Vec2/Vec3/Vec4 (16-byte aligned, padded), arithmetic, dot/cross/normalize"
```

---

## Task 6: `aleph.math:mat` — Mat3 + Mat4

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-mat.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_mat.cpp`
- Modify: `aleph.math.cppm`, `aleph.math/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 6.1: Write failing test `tests/math/test_mat.cpp`**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Mat4 layout: 64-byte aligned, fits cache line") {
    static_assert(sizeof(Mat4)  == 64);
    static_assert(alignof(Mat4) == 64);
}

TEST_CASE("Mat4 identity * Vec4 = Vec4") {
    constexpr Mat4 I = Mat4::identity();
    const Vec4 v{1, 2, 3, 1};
    const Vec4 r = I * v;
    CHECK(r == v);
}

TEST_CASE("Mat4 translate") {
    const Mat4 T = Mat4::translate({10, 20, 30});
    const Vec4 r = T * Vec4{1, 2, 3, 1};
    CHECK(r == Vec4{11, 22, 33, 1});
}

TEST_CASE("Mat4 scale") {
    const Mat4 S = Mat4::scale({2, 3, 4});
    const Vec4 r = S * Vec4{1, 1, 1, 1};
    CHECK(r == Vec4{2, 3, 4, 1});
}

TEST_CASE("Mat4 multiplication associative on identity") {
    const Mat4 T = Mat4::translate({1, 2, 3});
    const Mat4 I = Mat4::identity();
    const Mat4 R = T * I;
    for (int i = 0; i < 16; ++i) CHECK(R.m[i] == doctest::Approx(T.m[i]));
}

TEST_CASE("Mat4 look_at gives -Z forward") {
    const Mat4 V = Mat4::look_at({0,0,5}, {0,0,0}, {0,1,0});
    const Vec4 r = V * Vec4{0,0,0,1};
    CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-5.0f).epsilon(1e-5f));
}

TEST_CASE("Mat3 basics") {
    constexpr Mat3 I = Mat3::identity();
    const Vec3 v{1, 2, 3};
    const Vec3 r = I * v;
    CHECK(r == v);
}
```

- [ ] **Step 6.2: Run to see fail.**

- [ ] **Step 6.3: Write `aleph.math-mat.cppm`**

```cpp
module;
#include <array>
#include <cmath>

export module aleph.math:mat;

import :types;
import :vec;

export namespace aleph::math {

// 3×3 col-major. Padded to 3× Vec3 = 48 B with alignment 16.
struct alignas(16) Mat3 {
    std::array<f32, 12> m{};   // 3 columns × 4 floats (last = pad)

    static constexpr Mat3 identity() noexcept {
        Mat3 r{};
        r.m[0] = r.m[5] = r.m[10] = 1.0f;
        return r;
    }
};

[[nodiscard]] constexpr Vec3 operator*(const Mat3& a, Vec3 v) noexcept {
    return {
        a.m[0]*v.x + a.m[4]*v.y + a.m[8] *v.z,
        a.m[1]*v.x + a.m[5]*v.y + a.m[9] *v.z,
        a.m[2]*v.x + a.m[6]*v.y + a.m[10]*v.z,
    };
}

// 4×4 col-major. Element (row r, col c) at m[c*4 + r]. 64 B = 1 cache line.
struct alignas(64) Mat4 {
    std::array<f32, 16> m{};

    constexpr f32&       operator()(int r, int c)       noexcept { return m[c*4 + r]; }
    constexpr const f32& operator()(int r, int c) const noexcept { return m[c*4 + r]; }

    static constexpr Mat4 zero() noexcept { return Mat4{}; }

    static constexpr Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static constexpr Mat4 translate(Vec3 t) noexcept {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static constexpr Mat4 scale(Vec3 s) noexcept {
        Mat4 r{};
        r.m[0]  = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 rotate_x(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[5] = c;  r.m[6]  = s;
        r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotate_y(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c;  r.m[2]  = -s;
        r.m[8] = s;  r.m[10] = c;
        return r;
    }

    static Mat4 rotate_z(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c;  r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 perspective(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        Mat4 r{};
        const f32 fy = 1.0f / std::tan(fov_y * 0.5f);
        r.m[0]  = fy / aspect;
        r.m[5]  = fy;
        r.m[10] = (far + near) / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far * near) / (near - far);
        return r;
    }

    static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
        const Vec3 f = normalize(target - eye);
        const Vec3 s = normalize(cross(f, up));
        const Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0]  =  s.x; r.m[4]  =  s.y; r.m[8]   =  s.z;
        r.m[1]  =  u.x; r.m[5]  =  u.y; r.m[9]   =  u.z;
        r.m[2]  = -f.x; r.m[6]  = -f.y; r.m[10]  = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] =  dot(f, eye);
        return r;
    }
};

[[nodiscard]] constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r{};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            f32 s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k*4 + i] * b.m[j*4 + k];
            r.m[j*4 + i] = s;
        }
    return r;
}

[[nodiscard]] constexpr Vec4 operator*(const Mat4& a, Vec4 v) noexcept {
    return {
        a.m[0]*v.x + a.m[4]*v.y + a.m[8] *v.z + a.m[12]*v.w,
        a.m[1]*v.x + a.m[5]*v.y + a.m[9] *v.z + a.m[13]*v.w,
        a.m[2]*v.x + a.m[6]*v.y + a.m[10]*v.z + a.m[14]*v.w,
        a.m[3]*v.x + a.m[7]*v.y + a.m[11]*v.z + a.m[15]*v.w,
    };
}

}  // namespace aleph::math
```

- [ ] **Step 6.4: Wire it up**

Add `aleph.math-mat.cppm` to `aleph.math.cppm` (`export import :mat;`), to the `FILES` list in `aleph.math/CMakeLists.txt`, and `math/test_mat.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 6.5: Build + test** — all pass.

- [ ] **Step 6.6: Commit**

```bash
git add foundation/src/aleph.math/aleph.math-mat.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_mat.cpp tests/CMakeLists.txt
git commit -m "task 6: aleph.math:mat — Mat3 + Mat4 (col-major, perspective/look_at/rotate)"
```

---

## Task 7: `aleph.math:quat` — Quaternion (LEGACY interop)

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-quat.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_quat.cpp`
- Modify: primary unit, module CMake, tests CMake.

- [ ] **Step 7.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Quat layout: 16 bytes, alignas 16") {
    static_assert(sizeof(Quat)  == 16);
    static_assert(alignof(Quat) == 16);
}

TEST_CASE("Quat identity") {
    constexpr Quat q = Quat::identity();
    CHECK(q.x == 0.0f); CHECK(q.y == 0.0f); CHECK(q.z == 0.0f); CHECK(q.w == 1.0f);
}

TEST_CASE("Quat compose: identity * q = q") {
    const Quat a = Quat::identity();
    const Quat b{0.1f, 0.2f, 0.3f, 0.9273f};   // approx normalized
    const Quat c = a * b;
    CHECK(c.x == doctest::Approx(b.x));
    CHECK(c.y == doctest::Approx(b.y));
    CHECK(c.z == doctest::Approx(b.z));
    CHECK(c.w == doctest::Approx(b.w));
}

TEST_CASE("Quat rotate a vector") {
    // 90° around +y rotates (1,0,0) → (0,0,-1) (right-handed).
    const f32 half = 0.7071067811f;
    const Quat q{0.0f, half, 0.0f, half};
    const Vec3 r = apply(q, Vec3{1, 0, 0});
    CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-1.0f).epsilon(1e-5f));
}
```

- [ ] **Step 7.2: Run to see fail.**

- [ ] **Step 7.3: Write `aleph.math-quat.cppm`**

```cpp
module;
#include <cmath>

export module aleph.math:quat;

import :types;
import :vec;

export namespace aleph::math {

// LEGACY rotation type. Use Rotor for new code (task 8+).
// Layout: imaginary first (x, y, z), real last (w) — matches most assets.
struct alignas(16) Quat {
    f32 x{}, y{}, z{}, w{1.0f};

    static constexpr Quat identity() noexcept { return Quat{0, 0, 0, 1}; }
};

// Hamilton product: a * b (apply b first, then a).
[[nodiscard]] constexpr Quat operator*(Quat a, Quat b) noexcept {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
    };
}

// Rotate v by q: v' = q v q⁻¹. Assumes q is unit.
[[nodiscard]] inline Vec3 apply(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

}  // namespace aleph::math
```

- [ ] **Step 7.4: Wire + build + test + commit**

Add to primary unit, CMake FILES, tests/CMakeLists.txt.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-quat.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_quat.cpp tests/CMakeLists.txt
git commit -m "task 7: aleph.math:quat — Quaternion (LEGACY interop only)"
```

---

## Task 8: `aleph.math:rotor` — Rotor + compose

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-rotor.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_rotor.cpp`
- Modify: primary unit, module CMake, tests CMake.

- [ ] **Step 8.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Rotor layout: 16 bytes, alignas 16, trivially copyable") {
    static_assert(sizeof(Rotor)  == 16);
    static_assert(alignof(Rotor) == 16);
    static_assert(std::is_trivially_copyable_v<Rotor>);
}

TEST_CASE("Rotor identity") {
    constexpr Rotor R = Rotor::identity();
    CHECK(R.s   == 1.0f);
    CHECK(R.b12 == 0.0f);
    CHECK(R.b23 == 0.0f);
    CHECK(R.b31 == 0.0f);
}

TEST_CASE("Rotor compose: identity is neutral") {
    const Rotor I = Rotor::identity();
    const Rotor A{0.9f, 0.3f, 0.1f, 0.2f};
    const Rotor IA = I * A;
    const Rotor AI = A * I;
    CHECK(IA.s   == doctest::Approx(A.s));
    CHECK(IA.b12 == doctest::Approx(A.b12));
    CHECK(AI.s   == doctest::Approx(A.s));
    CHECK(AI.b23 == doctest::Approx(A.b23));
}

TEST_CASE("Rotor compose is associative") {
    const Rotor A{0.7f, 0.5f, 0.4f, 0.3f};
    const Rotor B{0.6f, 0.2f, 0.5f, 0.6f};
    const Rotor C{0.8f, 0.3f, 0.1f, 0.5f};
    const Rotor lhs = A * (B * C);
    const Rotor rhs = (A * B) * C;
    CHECK(approx_eq(lhs.s,   rhs.s,   1e-5f));
    CHECK(approx_eq(lhs.b12, rhs.b12, 1e-5f));
    CHECK(approx_eq(lhs.b23, rhs.b23, 1e-5f));
    CHECK(approx_eq(lhs.b31, rhs.b31, 1e-5f));
}
```

- [ ] **Step 8.2: Run to see fail.**

- [ ] **Step 8.3: Write `aleph.math-rotor.cppm`**

```cpp
module;
#include <type_traits>

export module aleph.math:rotor;

import :types;

export namespace aleph::math {

// G(3,0,0) even-grade element: scalar + bivector.
// Components in basis {1, e12, e23, e31}.
// Hot path: compose is 8 mul + 7 add. AVX2-friendly when batched.
struct alignas(16) Rotor {
    f32 s{1.0f};
    f32 b12{}, b23{}, b31{};

    static constexpr int grade = 0;   // even-grade: 0 + 2

    static constexpr Rotor identity() noexcept { return Rotor{1, 0, 0, 0}; }
};

// Geometric product of two rotors. Derived from the multiplication table
// of the even subalgebra of G(3,0,0). 8 mul + 7 add.
[[nodiscard]] constexpr Rotor operator*(Rotor a, Rotor b) noexcept {
    return {
        a.s*b.s   - a.b12*b.b12 - a.b23*b.b23 - a.b31*b.b31,
        a.s*b.b12 + a.b12*b.s   + a.b23*b.b31 - a.b31*b.b23,
        a.s*b.b23 - a.b12*b.b31 + a.b23*b.s   + a.b31*b.b12,
        a.s*b.b31 + a.b12*b.b23 - a.b23*b.b12 + a.b31*b.s,
    };
}

[[nodiscard]] constexpr Rotor reverse(Rotor R) noexcept {
    return Rotor{R.s, -R.b12, -R.b23, -R.b31};
}

}  // namespace aleph::math
```

- [ ] **Step 8.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-rotor.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_rotor.cpp tests/CMakeLists.txt
git commit -m "task 8: aleph.math:rotor — Rotor + geometric product (8 mul + 7 add) + reverse"
```

---

## Task 9: `aleph.math:bivector` — Bivector + Trivector + wedge

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-bivector.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_bivector.cpp`

- [ ] **Step 9.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Bivector layout: 12 bytes, natural alignment") {
    static_assert(sizeof(Bivector) == 12);
    static_assert(Bivector::grade == 2);
}

TEST_CASE("Trivector layout: 4 bytes, pseudoscalar") {
    static_assert(sizeof(Trivector) == 4);
    static_assert(Trivector::grade == 3);
}

TEST_CASE("wedge of parallel vectors is zero") {
    const Bivector b = wedge(Vec3{1, 0, 0}, Vec3{2, 0, 0});
    CHECK(approx_eq(b.e12, 0.0f));
    CHECK(approx_eq(b.e23, 0.0f));
    CHECK(approx_eq(b.e31, 0.0f));
}

TEST_CASE("wedge of x and y gives e12 = 1") {
    const Bivector b = wedge(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    CHECK(b.e12 == doctest::Approx(1.0f));
    CHECK(b.e23 == doctest::Approx(0.0f));
    CHECK(b.e31 == doctest::Approx(0.0f));
}

TEST_CASE("wedge is antisymmetric") {
    const Vec3 a{1, 2, 3}, b{4, 5, 6};
    const Bivector ab = wedge(a, b);
    const Bivector ba = wedge(b, a);
    CHECK(approx_eq(ab.e12, -ba.e12));
    CHECK(approx_eq(ab.e23, -ba.e23));
    CHECK(approx_eq(ab.e31, -ba.e31));
}
```

- [ ] **Step 9.2: Run to see fail.**

- [ ] **Step 9.3: Write `aleph.math-bivector.cppm`**

```cpp
export module aleph.math:bivector;

import :types;
import :vec;

export namespace aleph::math {

// Grade-2 element of G(3,0,0): coefficients on e12, e23, e31.
// A bivector encodes an oriented planar area.
struct Bivector {
    f32 e12{}, e23{}, e31{};
    static constexpr int grade = 2;

    constexpr Bivector operator+(Bivector b) const noexcept {
        return {e12 + b.e12, e23 + b.e23, e31 + b.e31};
    }
    constexpr Bivector operator*(f32 s) const noexcept {
        return {e12 * s, e23 * s, e31 * s};
    }
    constexpr Bivector operator-() const noexcept { return {-e12, -e23, -e31}; }
};

// Grade-3 element: the pseudoscalar e123.
struct Trivector {
    f32 e123{};
    static constexpr int grade = 3;
};

// Outer (wedge) product of two grade-1 vectors → bivector.
// e_i ∧ e_j = e_ij, e_i ∧ e_i = 0. Antisymmetric.
[[nodiscard]] constexpr Bivector wedge(Vec3 a, Vec3 b) noexcept {
    return {
        a.x*b.y - a.y*b.x,   // e12
        a.y*b.z - a.z*b.y,   // e23
        a.z*b.x - a.x*b.z,   // e31
    };
}

// Trivolume of three vectors = det.
[[nodiscard]] constexpr Trivector wedge(Vec3 a, Vec3 b, Vec3 c) noexcept {
    return { a.x*(b.y*c.z - b.z*c.y)
           - a.y*(b.x*c.z - b.z*c.x)
           + a.z*(b.x*c.y - b.y*c.x) };
}

}  // namespace aleph::math
```

- [ ] **Step 9.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-bivector.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_bivector.cpp tests/CMakeLists.txt
git commit -m "task 9: aleph.math:bivector — Bivector + Trivector + wedge (antisymmetric)"
```

---

## Task 10: `aleph.math:multivector` + `Vec3 * Vec3` + `Rotor::apply`

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-multivector.cppm`
- Modify: `aleph.math-rotor.cppm` (add `apply`)
- Create: `/home/lkz/aleph-cxx/tests/math/test_multivector.cpp`

- [ ] **Step 10.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Multivector layout: 8 floats, 32-byte aligned") {
    static_assert(sizeof(Multivector)  == 32);
    static_assert(alignof(Multivector) == 32);
}

TEST_CASE("Vec3 * Vec3 = dot scalar + wedge bivector") {
    const Vec3 a{1, 0, 0}, b{0, 1, 0};
    const Multivector m = a * b;
    CHECK(m.s   == doctest::Approx(0.0f));   // a·b = 0
    CHECK(m.e12 == doctest::Approx(1.0f));   // a∧b = e12
    CHECK(m.e23 == doctest::Approx(0.0f));
    CHECK(m.e31 == doctest::Approx(0.0f));
}

TEST_CASE("Rotor::apply rotates Vec3 by 180° around y") {
    // Bivector e31 corresponds to rotation in the x–z plane;
    // π rotation gives Rotor with s = 0, b31 = 1.
    const Rotor R{0.0f, 0.0f, 0.0f, 1.0f};   // 180° in x-z
    const Vec3 v{1, 0, 0};
    const Vec3 r = apply(R, v);
    CHECK(r.x == doctest::Approx(-1.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx( 0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx( 0.0f).epsilon(1e-5f));
}
```

- [ ] **Step 10.2: Run to see fail.**

- [ ] **Step 10.3: Write `aleph.math-multivector.cppm`**

```cpp
module;
#include <type_traits>

export module aleph.math:multivector;

import :types;
import :vec;
import :bivector;

export namespace aleph::math {

// Full dense G(3,0,0) element: {1, e1, e2, e3, e12, e23, e31, e123}.
// 32-byte aligned so 1 multivector = 1 __m256 (AVX2 lane).
struct alignas(32) Multivector {
    f32 s{};
    f32 e1{}, e2{}, e3{};
    f32 e12{}, e23{}, e31{};
    f32 e123{};
};

// Geometric product of two grade-1 vectors: a * b = a·b + a∧b.
[[nodiscard]] constexpr Multivector operator*(Vec3 a, Vec3 b) noexcept {
    Multivector m{};
    m.s   = a.x*b.x + a.y*b.y + a.z*b.z;
    m.e12 = a.x*b.y - a.y*b.x;
    m.e23 = a.y*b.z - a.z*b.y;
    m.e31 = a.z*b.x - a.x*b.z;
    return m;
}

}  // namespace aleph::math
```

- [ ] **Step 10.4: Add `apply` to `aleph.math-rotor.cppm`**

Append to the existing rotor module file, inside the `namespace aleph::math {}`:

```cpp
// Rotate v by R: v' = R v R⁻¹. Closed-form expansion of the sandwich
// for grade-1 input, avoiding the full multivector roundtrip.
[[nodiscard]] inline Vec3 apply(Rotor R, Vec3 v) noexcept {
    // Compute u = R v, where R = s + B (bivector). The grade-1 part of u:
    // u = s·v + B·v  (where B·v has grade 1 and grade 3 components).
    // Then u * reverse(R) gives back a grade-1 result + grade-3 cancels.
    // We expand the algebra directly:
    const f32 sx = R.s;
    const f32 bxy = R.b12, byz = R.b23, bzx = R.b31;
    // First half: t = R · v as multivector grade {1, 3}.
    const f32 t_x =  sx*v.x + bxy*v.y - bzx*v.z;
    const f32 t_y =  sx*v.y - bxy*v.x + byz*v.z;
    const f32 t_z =  sx*v.z - byz*v.y + bzx*v.x;
    const f32 t_xyz = bxy*v.z + byz*v.x + bzx*v.y;
    // Second half: u = t · reverse(R).
    return Vec3{
         t_x*sx + t_y*bxy - t_z*bzx + t_xyz*byz,
         t_y*sx - t_x*bxy + t_z*byz + t_xyz*bzx,
         t_z*sx + t_x*bzx - t_y*byz + t_xyz*bxy,
    };
}
```

- [ ] **Step 10.5: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-multivector.cppm foundation/src/aleph.math/aleph.math-rotor.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_multivector.cpp tests/CMakeLists.txt
git commit -m "task 10: aleph.math:multivector + Vec3*Vec3 + Rotor::apply (sandwich product)"
```

---

## Task 11: `aleph.math:rotor` advanced — `from_axis_angle`, `slerp`, `to_mat3`, `from_quat`

**Files:**
- Modify: `aleph.math-rotor.cppm` (extend)
- Create: `/home/lkz/aleph-cxx/tests/math/test_rotor_advanced.cpp`

- [ ] **Step 11.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("from_axis_angle: 90° around +y") {
    const Rotor R = from_axis_angle(Vec3{0, 1, 0}, 1.57079632679f);  // π/2
    const Vec3  r = apply(R, Vec3{1, 0, 0});
    CHECK(r.x == doctest::Approx( 0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("slerp endpoints") {
    const Rotor A = from_axis_angle({0, 1, 0}, 0.0f);
    const Rotor B = from_axis_angle({0, 1, 0}, 1.0f);
    const Rotor s0 = slerp(A, B, 0.0f);
    const Rotor s1 = slerp(A, B, 1.0f);
    CHECK(approx_eq(s0.s,   A.s,   1e-5f));
    CHECK(approx_eq(s1.s,   B.s,   1e-5f));
    CHECK(approx_eq(s1.b23, B.b23, 1e-5f));
}

TEST_CASE("to_mat3 of identity is identity matrix") {
    const Mat3 M = to_mat3(Rotor::identity());
    CHECK(M.m[0]  == doctest::Approx(1.0f));
    CHECK(M.m[5]  == doctest::Approx(1.0f));
    CHECK(M.m[10] == doctest::Approx(1.0f));
}

TEST_CASE("from_quat round-trips through to_mat3 vs Quat apply") {
    const f32 half = 0.7071067811f;
    const Quat  q{0.0f, half, 0.0f, half};      // 90° around +y
    const Rotor R = from_quat(q);
    const Vec3  v{1, 0, 0};
    const Vec3  rq = apply(q, v);
    const Vec3  rr = apply(R, v);
    CHECK(rq.x == doctest::Approx(rr.x).epsilon(1e-5f));
    CHECK(rq.y == doctest::Approx(rr.y).epsilon(1e-5f));
    CHECK(rq.z == doctest::Approx(rr.z).epsilon(1e-5f));
}
```

- [ ] **Step 11.2: Run to see fail.**

- [ ] **Step 11.3: Extend `aleph.math-rotor.cppm`**

Add to the module's global module fragment:
```cpp
#include <cmath>
```

Add new imports inside the module purview:
```cpp
import :vec;
import :mat;
import :quat;
```

Append to the `namespace aleph::math {}`:

```cpp
[[nodiscard]] inline Rotor from_axis_angle(Vec3 axis, f32 rad) noexcept {
    // axis must be unit. The rotation plane is the dual of `axis`.
    // dual: (x,y,z) → (B23=x, B31=y, B12=z) under e123.
    const f32 half = 0.5f * rad;
    const f32 c = std::cos(half);
    const f32 s = std::sin(half);
    return Rotor{ c, s * axis.z, s * axis.x, s * axis.y };
}

// Spherical linear interpolation between two unit rotors.
[[nodiscard]] inline Rotor slerp(Rotor a, Rotor b, f32 t) noexcept {
    // Treat rotors as unit 4-vectors in {s, b12, b23, b31}.
    f32 d = a.s*b.s + a.b12*b.b12 + a.b23*b.b23 + a.b31*b.b31;
    Rotor bb = b;
    if (d < 0.0f) {
        bb = Rotor{-b.s, -b.b12, -b.b23, -b.b31};
        d = -d;
    }
    if (d > 0.9995f) {
        // Vectors nearly parallel — linear interp + renorm.
        Rotor r{
            a.s   + t * (bb.s   - a.s),
            a.b12 + t * (bb.b12 - a.b12),
            a.b23 + t * (bb.b23 - a.b23),
            a.b31 + t * (bb.b31 - a.b31),
        };
        const f32 inv = 1.0f / std::sqrt(r.s*r.s + r.b12*r.b12 + r.b23*r.b23 + r.b31*r.b31);
        return {r.s*inv, r.b12*inv, r.b23*inv, r.b31*inv};
    }
    const f32 theta_0 = std::acos(d);
    const f32 sin_0   = std::sin(theta_0);
    const f32 theta   = theta_0 * t;
    const f32 wa      = std::sin(theta_0 - theta) / sin_0;
    const f32 wb      = std::sin(theta)           / sin_0;
    return Rotor{
        wa*a.s   + wb*bb.s,
        wa*a.b12 + wb*bb.b12,
        wa*a.b23 + wb*bb.b23,
        wa*a.b31 + wb*bb.b31,
    };
}

// Convert rotor to its equivalent 3×3 rotation matrix.
[[nodiscard]] inline Mat3 to_mat3(Rotor R) noexcept {
    const f32 s = R.s;
    const f32 b12 = R.b12, b23 = R.b23, b31 = R.b31;
    Mat3 m{};
    m.m[0]  = 1 - 2*(b12*b12 + b31*b31);
    m.m[1]  =     2*(b12*s    + b23*b31);
    m.m[2]  =     2*(b31*s    - b12*b23);
    m.m[4]  =     2*(b12*s    - b23*b31);  // wait — col 1
    // (full 9 entries; written carefully:)
    m.m[4]  =     2*(b12*(-s) + b23*b31);
    m.m[5]  = 1 - 2*(b12*b12 + b23*b23);
    m.m[6]  =     2*(b23*s    + b12*b31);
    m.m[8]  =     2*(b31*s    + b12*b23);
    m.m[9]  =     2*(b23*(-s) + b12*b31);
    m.m[10] = 1 - 2*(b23*b23 + b31*b31);
    return m;
}

// Quat (x,y,z,w) → Rotor with the same rotation.
// q = (qw + qx·e23 + qy·e31 + qz·e12) once you map quaternion basis to GA bivectors.
[[nodiscard]] constexpr Rotor from_quat(Quat q) noexcept {
    return Rotor{ q.w, q.z, q.x, q.y };
}
```

- [ ] **Step 11.4: Build + test**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
```

If `to_mat3` test fails on off-diagonal entries (likely — formula is fiddly), the test catches it and you fix the signs. Expected: green.

- [ ] **Step 11.5: Commit**

```bash
git add foundation/src/aleph.math/aleph.math-rotor.cppm tests/math/test_rotor_advanced.cpp tests/CMakeLists.txt
git commit -m "task 11: aleph.math:rotor — from_axis_angle, slerp, to_mat3, from_quat"
```

---

## Task 12: `aleph.math:dual` — forward-mode autodiff

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-dual.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_dual.cpp`

- [ ] **Step 12.1: Write failing test**

```cpp
#include "doctest.h"
#include <cmath>
import aleph.math;

using namespace aleph::math;

TEST_CASE("Dual<f32>: identity dual evaluates value + eps") {
    constexpr Dual<f32> x{3.0f, 1.0f};   // x = 3, dx/dx = 1
    CHECK(x.val == 3.0f);
    CHECK(x.eps == 1.0f);
}

TEST_CASE("Dual<f32> + - * / propagate derivatives correctly") {
    const Dual<f32> x{2.0f, 1.0f};
    const Dual<f32> y{3.0f, 0.0f};   // constant

    const Dual<f32> sum  = x + y;
    const Dual<f32> diff = x - y;
    const Dual<f32> prod = x * y;
    const Dual<f32> quot = x / y;

    CHECK(sum.val  == doctest::Approx(5.0f));
    CHECK(sum.eps  == doctest::Approx(1.0f));         // d(x+y)/dx = 1
    CHECK(diff.eps == doctest::Approx(1.0f));         // d(x-y)/dx = 1
    CHECK(prod.eps == doctest::Approx(3.0f));         // d(xy)/dx = y = 3
    CHECK(quot.eps == doctest::Approx(1.0f/3.0f));    // d(x/y)/dx = 1/y
}

TEST_CASE("dual sin: d(sin x)/dx = cos x") {
    const Dual<f32> x{1.0f, 1.0f};
    const Dual<f32> y = sin(x);
    CHECK(y.val == doctest::Approx(std::sin(1.0f)));
    CHECK(y.eps == doctest::Approx(std::cos(1.0f)));
}

TEST_CASE("Dual<Vec3> normalize derivative is unit-circle Jacobian") {
    // Sanity: normalize at (3,0,0) with seed (1,0,0) → (0, 0, 0)
    // because moving along the radial direction doesn't change unit normal.
    const Dual<Vec3> v{Vec3{3, 0, 0}, Vec3{1, 0, 0}};
    const Dual<Vec3> n = normalize(v);
    CHECK(n.val == Vec3{1, 0, 0});
    CHECK(approx_eq(n.eps.x, 0.0f, 1e-5f));
    CHECK(approx_eq(n.eps.y, 0.0f, 1e-5f));
    CHECK(approx_eq(n.eps.z, 0.0f, 1e-5f));
}
```

- [ ] **Step 12.2: Run to see fail.**

- [ ] **Step 12.3: Write `aleph.math-dual.cppm`**

```cpp
module;
#include <cmath>

export module aleph.math:dual;

import :types;
import :vec;

export namespace aleph::math {

// Forward-mode dual number: x = val + eps·ε, where ε² = 0.
// f(x) = f(val) + f'(val) · eps.
template<typename T>
struct Dual {
    T val{};
    T eps{};
};

// ─── scalar ops ─────────────────────────────────────────────────────
constexpr Dual<f32> operator+(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val + b.val, a.eps + b.eps};
}
constexpr Dual<f32> operator-(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val - b.val, a.eps - b.eps};
}
constexpr Dual<f32> operator*(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val * b.val, a.val * b.eps + a.eps * b.val};
}
constexpr Dual<f32> operator/(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val / b.val,
            (a.eps * b.val - a.val * b.eps) / (b.val * b.val)};
}

inline Dual<f32> sin(Dual<f32> x) noexcept {
    return {std::sin(x.val), std::cos(x.val) * x.eps};
}
inline Dual<f32> cos(Dual<f32> x) noexcept {
    return {std::cos(x.val), -std::sin(x.val) * x.eps};
}
inline Dual<f32> sqrt(Dual<f32> x) noexcept {
    const f32 v = std::sqrt(x.val);
    return {v, x.eps * (0.5f / v)};
}

// ─── vector ops ─────────────────────────────────────────────────────
constexpr Dual<Vec3> operator+(Dual<Vec3> a, Dual<Vec3> b) noexcept {
    return {a.val + b.val, a.eps + b.eps};
}
constexpr Dual<Vec3> operator-(Dual<Vec3> a, Dual<Vec3> b) noexcept {
    return {a.val - b.val, a.eps - b.eps};
}
constexpr Dual<Vec3> operator*(Dual<Vec3> a, f32 s) noexcept {
    return {a.val * s, a.eps * s};
}

// Dual normalize: d/dt [v/|v|] = (I - n n^T) · dv / |v|.
inline Dual<Vec3> normalize(Dual<Vec3> v) noexcept {
    const f32 inv_len = 1.0f / length(v.val);
    const Vec3 n      = v.val * inv_len;
    // tangent component of v.eps with respect to the unit sphere at n
    const f32  proj   = dot(v.eps, n);
    const Vec3 deps   = (v.eps - n * proj) * inv_len;
    return {n, deps};
}

}  // namespace aleph::math
```

- [ ] **Step 12.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-dual.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_dual.cpp tests/CMakeLists.txt
git commit -m "task 12: aleph.math:dual — forward-mode autodiff (scalar + vec, sin/cos/sqrt/normalize)"
```

---

## Task 13: `aleph.math:tangent` — `TangentVec<Manifold>` tag-distinct from Vec3

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-tangent.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_tangent.cpp`

- [ ] **Step 13.1: Write failing test**

```cpp
#include "doctest.h"
#include <type_traits>
import aleph.math;

using namespace aleph::math;

TEST_CASE("TangentR3 and TangentSurface are distinct types") {
    static_assert(!std::is_same_v<TangentR3, TangentSurface>);
    static_assert(!std::is_same_v<TangentR3, Vec3>);
}

TEST_CASE("TangentR3 wraps Vec3, exposes .v") {
    const TangentR3 t{Vec3{1, 2, 3}};
    CHECK(t.v == Vec3{1, 2, 3});
}

TEST_CASE("project_to_tangent removes normal component") {
    const Vec3 n{0, 1, 0};        // surface normal
    const Vec3 dir{1, 1, 0};      // direction with y component
    const TangentSurface t = project_to_tangent(dir, n);
    CHECK(approx_eq(t.v.y, 0.0f, 1e-6f));   // y stripped
    CHECK(t.v.x == doctest::Approx(1.0f));
    CHECK(t.v.z == doctest::Approx(0.0f));
}
```

- [ ] **Step 13.2: Run to see fail.**

- [ ] **Step 13.3: Write `aleph.math-tangent.cppm`**

```cpp
export module aleph.math:tangent;

import :types;
import :vec;

export namespace aleph::math {

// Phantom tag types — never instantiated, used only to distinguish
// otherwise-identical TangentVec types at the type level.
struct R3;
struct Surf;

// A tangent vector at a point on a manifold. Wraps a Vec3 but is NOT
// convertible to/from Vec3 implicitly — operations that mix tangent
// frames must go through explicit transforms.
template<typename Manifold>
struct TangentVec {
    Vec3 v{};

    constexpr TangentVec operator+(TangentVec b) const noexcept { return {v + b.v}; }
    constexpr TangentVec operator-(TangentVec b) const noexcept { return {v - b.v}; }
    constexpr TangentVec operator*(f32 s)        const noexcept { return {v * s}; }
};

using TangentR3      = TangentVec<R3>;
using TangentSurface = TangentVec<Surf>;

// Strip the normal component of `dir`, returning the tangent-plane vector.
// `n` must be unit.
[[nodiscard]] constexpr TangentSurface
project_to_tangent(Vec3 dir, Vec3 n) noexcept {
    return TangentSurface{ dir - n * dot(dir, n) };
}

}  // namespace aleph::math
```

- [ ] **Step 13.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-tangent.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_tangent.cpp tests/CMakeLists.txt
git commit -m "task 13: aleph.math:tangent — TangentVec<Manifold> tag-distinct from Vec3"
```

---

## Task 14: `aleph.math:geom` — AABB, Ray, Plane

**Files:**
- Create: `/home/lkz/aleph-cxx/foundation/src/aleph.math/aleph.math-geom.cppm`
- Create: `/home/lkz/aleph-cxx/tests/math/test_geom.cpp`

- [ ] **Step 14.1: Write failing test**

```cpp
#include "doctest.h"
#include <array>
import aleph.math;

using namespace aleph::math;

TEST_CASE("Aabb basic construction + union") {
    constexpr Aabb a{{-1, -1, -1}, {1, 1, 1}};
    constexpr Aabb b{{ 0,  0,  0}, {2, 2, 2}};
    constexpr Aabb u = union_of(a, b);
    CHECK(u.min == Vec3{-1, -1, -1});
    CHECK(u.max == Vec3{ 2,  2,  2});
}

TEST_CASE("Aabb from_points") {
    const std::array<Vec3, 3> pts{ Vec3{1,0,0}, Vec3{0,2,0}, Vec3{-1,1,3} };
    const Aabb b = Aabb::from_points(pts);
    CHECK(b.min == Vec3{-1, 0, 0});
    CHECK(b.max == Vec3{ 1, 2, 3});
}

TEST_CASE("Ray::at evaluates O + t·D") {
    constexpr Ray r{{0, 0, 0}, {1, 0, 0}};
    CHECK(r.at(2.5f) == Vec3{2.5f, 0, 0});
}

TEST_CASE("Plane: distance + classify") {
    const Plane p = Plane::from_point_normal({0, 0, 0}, {0, 1, 0});
    CHECK(p.distance({1, 3, 5})  == doctest::Approx( 3.0f));
    CHECK(p.distance({1, -2, 5}) == doctest::Approx(-2.0f));
    CHECK(classify(p, Vec3{0, 1, 0})   == PlaneSide::Front);
    CHECK(classify(p, Vec3{0, -1, 0})  == PlaneSide::Behind);
    CHECK(classify(p, Vec3{0, 0, 0})   == PlaneSide::On);
}
```

- [ ] **Step 14.2: Run to see fail.**

- [ ] **Step 14.3: Write `aleph.math-geom.cppm`**

```cpp
module;
#include <span>
#include <algorithm>

export module aleph.math:geom;

import :types;
import :vec;

export namespace aleph::math {

struct alignas(16) Aabb {
    Vec3 min{};
    Vec3 max{};

    constexpr Aabb() = default;
    constexpr Aabb(Vec3 mn, Vec3 mx) noexcept : min(mn), max(mx) {}

    constexpr Aabb translated(Vec3 t) const noexcept { return {min + t, max + t}; }

    static constexpr Aabb from_points(std::span<const Vec3> pts) noexcept {
        Aabb b{pts.front(), pts.front()};
        for (const Vec3& p : pts.subspan(1)) {
            b.min.x = std::min(b.min.x, p.x);
            b.min.y = std::min(b.min.y, p.y);
            b.min.z = std::min(b.min.z, p.z);
            b.max.x = std::max(b.max.x, p.x);
            b.max.y = std::max(b.max.y, p.y);
            b.max.z = std::max(b.max.z, p.z);
        }
        return b;
    }
};

[[nodiscard]] constexpr Aabb union_of(Aabb a, Aabb b) noexcept {
    return {
        Vec3{ std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z) },
        Vec3{ std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z) },
    };
}

struct alignas(16) Ray {
    Vec3 origin{};
    Vec3 dir{};

    constexpr Vec3 at(f32 t) const noexcept { return origin + dir * t; }
};

struct alignas(16) Plane {
    Vec3 normal{};
    f32  d{};

    static constexpr Plane from_point_normal(Vec3 p, Vec3 n) noexcept {
        return { n, -dot(n, p) };
    }
    constexpr f32 distance(Vec3 pt) const noexcept {
        return dot(normal, pt) + d;
    }
};

enum class PlaneSide { Behind = -1, On = 0, Front = 1 };

constexpr PlaneSide classify(Plane pl, Vec3 pt) noexcept {
    const f32 d = pl.distance(pt);
    if (d >  1e-4f) return PlaneSide::Front;
    if (d < -1e-4f) return PlaneSide::Behind;
    return PlaneSide::On;
}

}  // namespace aleph::math
```

- [ ] **Step 14.4: Update primary unit with all math partitions**

`aleph.math.cppm`:
```cpp
export module aleph.math;
export import :types;
export import :concepts;
export import :vec;
export import :mat;
export import :quat;
export import :rotor;
export import :bivector;
export import :multivector;
export import :dual;
export import :tangent;
export import :geom;
```

- [ ] **Step 14.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.math/aleph.math-geom.cppm foundation/src/aleph.math/aleph.math.cppm foundation/src/aleph.math/CMakeLists.txt tests/math/test_geom.cpp tests/CMakeLists.txt
git commit -m "task 14: aleph.math:geom — Aabb (+from_points/union_of), Ray, Plane (+classify); math module complete"
```

---

## Task 15: `aleph.alloc:arena` — bump-pointer arena

**Files:**
- Create: `foundation/src/aleph.alloc/CMakeLists.txt`
- Create: `foundation/src/aleph.alloc/aleph.alloc.cppm`
- Create: `foundation/src/aleph.alloc/aleph.alloc-arena.cppm`
- Create: `tests/alloc/test_arena.cpp`
- Modify: `foundation/CMakeLists.txt` (add subdir)

- [ ] **Step 15.1: Write failing test**

```cpp
#include "doctest.h"
#include <cstring>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Arena allocates contiguous bytes") {
    alignas(16) static unsigned char backing[4096];
    Arena a{backing, sizeof(backing)};

    void* p1 = a.allocate(64, 16);
    void* p2 = a.allocate(64, 16);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    CHECK(static_cast<unsigned char*>(p2) - static_cast<unsigned char*>(p1) == 64);
    CHECK(a.bytes_in_use() >= 128);
}

TEST_CASE("Arena alignment respected") {
    alignas(64) static unsigned char backing[4096];
    Arena a{backing, sizeof(backing)};
    void* p = a.allocate(7, 32);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 32 == 0);
}

TEST_CASE("Arena returns nullptr on OOM (no throw)") {
    alignas(16) static unsigned char backing[128];
    Arena a{backing, sizeof(backing)};
    CHECK(a.allocate(64, 16) != nullptr);
    CHECK(a.allocate(64, 16) != nullptr);
    CHECK(a.allocate(64, 16) == nullptr);   // exhausted
}

TEST_CASE("Arena::reset rewinds the bump pointer") {
    alignas(16) static unsigned char backing[1024];
    Arena a{backing, sizeof(backing)};
    a.allocate(256, 16);
    CHECK(a.bytes_in_use() >= 256);
    a.reset();
    CHECK(a.bytes_in_use() == 0);
    CHECK(a.allocate(256, 16) != nullptr);
}
```

- [ ] **Step 15.2: Run to see fail.**

- [ ] **Step 15.3: Write `aleph.alloc-arena.cppm`**

```cpp
module;
#include <cstddef>
#include <cstdint>
#include <memory_resource>

export module aleph.alloc:arena;

export namespace aleph::alloc {

// Bump-pointer arena over caller-provided storage. Single-thread.
// All ops noexcept. Allocation failure returns nullptr.
class Arena final : public std::pmr::memory_resource {
public:
    Arena(void* buffer, std::size_t size) noexcept
        : base_{static_cast<unsigned char*>(buffer)}, cap_{size} {}

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base_) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        peak_ = off_ > peak_ ? off_ : peak_;
        return reinterpret_cast<void*>(aligned);
    }

    void reset() noexcept { off_ = 0; }

    std::size_t bytes_in_use() const noexcept { return off_; }
    std::size_t peak_in_use()  const noexcept { return peak_; }
    std::size_t capacity()     const noexcept { return cap_; }

protected:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        return allocate(bytes, align);
    }
    void do_deallocate(void*, std::size_t, std::size_t) noexcept override { /* no-op */ }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    std::size_t    peak_{0};
};

}  // namespace aleph::alloc
```

- [ ] **Step 15.4: Write `aleph.alloc.cppm` + CMake**

`aleph.alloc.cppm`:
```cpp
export module aleph.alloc;
export import :arena;
```

`foundation/src/aleph.alloc/CMakeLists.txt`:
```cmake
add_library(aleph_alloc)
target_sources(aleph_alloc
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.alloc.cppm
        aleph.alloc-arena.cppm)
target_link_libraries(aleph_alloc
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_strict)
```

Append `add_subdirectory(src/aleph.alloc)` to `foundation/CMakeLists.txt`. Append `aleph_alloc` to test link libs and `alloc/test_arena.cpp` to test sources.

- [ ] **Step 15.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.alloc/ foundation/CMakeLists.txt tests/alloc/test_arena.cpp tests/CMakeLists.txt
git commit -m "task 15: aleph.alloc:arena — bump-pointer Arena (std::pmr::memory_resource)"
```

---

## Task 16: `aleph.alloc:frame` — double-buffered frame allocator

**Files:**
- Create: `foundation/src/aleph.alloc/aleph.alloc-frame.cppm`
- Create: `tests/alloc/test_frame.cpp`

- [ ] **Step 16.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Frame: write to A, release, write to B, release recycles A") {
    alignas(16) static unsigned char backing_a[1024];
    alignas(16) static unsigned char backing_b[1024];
    Frame f{backing_a, backing_b, 1024};

    void* p1 = f.allocate(256, 16);
    CHECK(p1 != nullptr);
    CHECK(f.bytes_in_use_current() == 256);

    f.release_frame();   // flip to backing_b
    CHECK(f.bytes_in_use_current() == 0);

    void* p2 = f.allocate(512, 16);
    CHECK(p2 != nullptr);
    CHECK(p2 != p1);                  // different buffer

    f.release_frame();   // flip back to backing_a
    void* p3 = f.allocate(64, 16);
    CHECK(p3 == p1);                  // recycled the first buffer's start
}
```

- [ ] **Step 16.2: Run to see fail.**

- [ ] **Step 16.3: Write `aleph.alloc-frame.cppm`**

```cpp
module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:frame;

export namespace aleph::alloc {

// Double-buffered bump allocator. release_frame() swaps the active buffer
// and resets it — the just-released buffer remains valid until the NEXT
// release (giving render code a frame of grace to read its results).
class Frame {
public:
    Frame(void* buf_a, void* buf_b, std::size_t size_each) noexcept
        : bufs_{static_cast<unsigned char*>(buf_a),
                static_cast<unsigned char*>(buf_b)},
          cap_{size_each} {}

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) noexcept {
        unsigned char* base = bufs_[active_];
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    void release_frame() noexcept {
        active_ ^= 1;
        off_ = 0;
    }

    std::size_t bytes_in_use_current() const noexcept { return off_; }
    std::size_t capacity()             const noexcept { return cap_; }

private:
    unsigned char* bufs_[2];
    std::size_t    cap_{0};
    std::size_t    off_{0};
    int            active_{0};
};

}  // namespace aleph::alloc
```

- [ ] **Step 16.4: Wire + build + test + commit**

Append to `aleph.alloc.cppm` (`export import :frame;`) and the CMake FILES list.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.alloc/aleph.alloc-frame.cppm foundation/src/aleph.alloc/aleph.alloc.cppm foundation/src/aleph.alloc/CMakeLists.txt tests/alloc/test_frame.cpp tests/CMakeLists.txt
git commit -m "task 16: aleph.alloc:frame — double-buffered Frame allocator"
```

---

## Task 17: `aleph.alloc:slab` — fixed-size slab with intrusive free-list

**Files:**
- Create: `foundation/src/aleph.alloc/aleph.alloc-slab.cppm`
- Create: `tests/alloc/test_slab.cpp`

- [ ] **Step 17.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Slab<32> allocates and recycles fixed-size blocks") {
    alignas(64) static unsigned char backing[4096];
    Slab<32> s{backing, sizeof(backing)};

    void* a = s.allocate();
    void* b = s.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);

    s.deallocate(a);
    void* c = s.allocate();
    CHECK(c == a);   // free-list reuses last released
}

TEST_CASE("Slab returns nullptr when exhausted") {
    alignas(64) static unsigned char backing[64];   // ≈ 2 slots of 32
    Slab<32> s{backing, sizeof(backing)};
    CHECK(s.allocate() != nullptr);
    CHECK(s.allocate() != nullptr);
    CHECK(s.allocate() == nullptr);
}
```

- [ ] **Step 17.2: Run to see fail.**

- [ ] **Step 17.3: Write `aleph.alloc-slab.cppm`**

```cpp
module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:slab;

export namespace aleph::alloc {

// Fixed-size slab. Each block is `BlockSize` bytes, aligned to `BlockSize`
// (rounded up to power of two). Released blocks are linked via an
// intrusive single-linked free list embedded in the block.
template<std::size_t BlockSize>
class Slab {
    static_assert(BlockSize >= sizeof(void*),
                  "BlockSize must hold at least a pointer");
public:
    Slab(void* buffer, std::size_t total_bytes) noexcept
        : base_{static_cast<unsigned char*>(buffer)},
          cap_{(total_bytes / BlockSize) * BlockSize} {}

    [[nodiscard]] void* allocate() noexcept {
        if (free_head_) {
            void* p = free_head_;
            free_head_ = *static_cast<void**>(p);
            return p;
        }
        if (off_ + BlockSize > cap_) return nullptr;
        void* p = base_ + off_;
        off_ += BlockSize;
        return p;
    }

    void deallocate(void* p) noexcept {
        *static_cast<void**>(p) = free_head_;
        free_head_ = p;
    }

    std::size_t capacity_blocks() const noexcept { return cap_ / BlockSize; }

private:
    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    void*          free_head_{nullptr};
};

}  // namespace aleph::alloc
```

- [ ] **Step 17.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.alloc/aleph.alloc-slab.cppm foundation/src/aleph.alloc/aleph.alloc.cppm foundation/src/aleph.alloc/CMakeLists.txt tests/alloc/test_slab.cpp tests/CMakeLists.txt
git commit -m "task 17: aleph.alloc:slab — Slab<N> fixed-size, intrusive free-list"
```

---

## Task 18: `aleph.alloc:freelist` — size-segregated free-list

**Files:**
- Create: `foundation/src/aleph.alloc/aleph.alloc-freelist.cppm`
- Create: `tests/alloc/test_freelist.cpp`

- [ ] **Step 18.1: Write failing test**

```cpp
#include "doctest.h"
#include <cstring>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("FreeList returns blocks aligned to request power of two") {
    alignas(64) static unsigned char backing[8192];
    FreeList f{backing, sizeof(backing)};

    void* p8   = f.allocate(8);
    void* p16  = f.allocate(16);
    void* p64  = f.allocate(64);
    void* p128 = f.allocate(128);
    REQUIRE(p8 && p16 && p64 && p128);

    CHECK(reinterpret_cast<std::uintptr_t>(p8)   % 8   == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p16)  % 16  == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p64)  % 64  == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p128) % 64  == 0);

    f.deallocate(p64, 64);
    void* p64b = f.allocate(64);
    CHECK(p64b == p64);   // segregated list recycles
}
```

- [ ] **Step 18.2: Run to see fail.**

- [ ] **Step 18.3: Write `aleph.alloc-freelist.cppm`**

```cpp
module;
#include <cstddef>
#include <cstdint>

export module aleph.alloc:freelist;

export namespace aleph::alloc {

// Mixed-size with size-segregated free lists for the buckets
// {8, 16, 32, 64, 128, 256}. Requests above 256 fall through to bump.
class FreeList {
public:
    static constexpr int N_BUCKETS = 6;
    static constexpr std::size_t bucket_size(int i) noexcept {
        return 1u << (i + 3);   // 8, 16, 32, 64, 128, 256
    }

    FreeList(void* buffer, std::size_t total_bytes) noexcept
        : base_{static_cast<unsigned char*>(buffer)}, cap_{total_bytes} {}

    [[nodiscard]] void* allocate(std::size_t bytes) noexcept {
        const int b = bucket_for(bytes);
        if (b >= 0) {
            if (heads_[b]) {
                void* p = heads_[b];
                heads_[b] = *static_cast<void**>(p);
                return p;
            }
            return bump(bucket_size(b), bucket_align(b));
        }
        return bump(bytes, 64);   // oversized → 64-byte aligned bump
    }

    void deallocate(void* p, std::size_t bytes) noexcept {
        const int b = bucket_for(bytes);
        if (b >= 0) {
            *static_cast<void**>(p) = heads_[b];
            heads_[b] = p;
        }
        // oversized: leak in bump region; reclaimed by reset() (not exposed here)
    }

private:
    static constexpr int bucket_for(std::size_t bytes) noexcept {
        for (int i = 0; i < N_BUCKETS; ++i)
            if (bytes <= bucket_size(i)) return i;
        return -1;
    }
    static constexpr std::size_t bucket_align(int i) noexcept {
        const std::size_t s = bucket_size(i);
        return s < 64 ? s : 64;
    }

    void* bump(std::size_t bytes, std::size_t align) noexcept {
        const std::uintptr_t cur = reinterpret_cast<std::uintptr_t>(base_) + off_;
        const std::uintptr_t aligned = (cur + (align - 1)) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (off_ + pad + bytes > cap_) return nullptr;
        off_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    unsigned char* base_{nullptr};
    std::size_t    cap_{0};
    std::size_t    off_{0};
    void*          heads_[N_BUCKETS]{};
};

}  // namespace aleph::alloc
```

- [ ] **Step 18.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.alloc/aleph.alloc-freelist.cppm foundation/src/aleph.alloc/aleph.alloc.cppm foundation/src/aleph.alloc/CMakeLists.txt tests/alloc/test_freelist.cpp tests/CMakeLists.txt
git commit -m "task 18: aleph.alloc:freelist — size-segregated free-list (8/16/32/64/128/256 B)"
```

---

## Task 19: `aleph.alloc:pmr_adapter` — pmr-allocator wrapper

**Files:**
- Create: `foundation/src/aleph.alloc/aleph.alloc-pmr_adapter.cppm`
- Create: `tests/alloc/test_pmr_adapter.cpp`

- [ ] **Step 19.1: Write failing test**

```cpp
#include "doctest.h"
#include <vector>
#include <memory_resource>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Arena works with std::pmr::vector") {
    alignas(16) static unsigned char backing[8192];
    Arena a{backing, sizeof(backing)};

    std::pmr::vector<int> v{&a};
    for (int i = 0; i < 100; ++i) v.push_back(i);
    CHECK(v.size() == 100);
    CHECK(v[42] == 42);
    CHECK(a.bytes_in_use() > 0);
}
```

- [ ] **Step 19.2: Run to see fail (header missing)**

- [ ] **Step 19.3: Write `aleph.alloc-pmr_adapter.cppm`**

Arena already inherits from `std::pmr::memory_resource`, so the adapter is trivial — just re-export the alias for ergonomics:

```cpp
module;
#include <memory_resource>

export module aleph.alloc:pmr_adapter;

export namespace aleph::alloc {

// Convenience alias for stdlib containers.
template<typename T>
using pmr_allocator = std::pmr::polymorphic_allocator<T>;

// Sanity ctor adapter: takes any of our resources and returns the same
// memory_resource* pointer typed as the stdlib base.
template<typename Resource>
inline std::pmr::memory_resource* as_resource(Resource& r) noexcept {
    return &r;
}

}  // namespace aleph::alloc
```

- [ ] **Step 19.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.alloc/aleph.alloc-pmr_adapter.cppm foundation/src/aleph.alloc/aleph.alloc.cppm foundation/src/aleph.alloc/CMakeLists.txt tests/alloc/test_pmr_adapter.cpp tests/CMakeLists.txt
git commit -m "task 19: aleph.alloc:pmr_adapter — alias for stdlib container interop; alloc module complete"
```

---

## Task 20: `aleph.containers:small_vector` — inline-N storage with heap fallback

**Files:**
- Create: `foundation/src/aleph.containers/CMakeLists.txt`
- Create: `foundation/src/aleph.containers/aleph.containers.cppm`
- Create: `foundation/src/aleph.containers/aleph.containers-small_vector.cppm`
- Create: `tests/containers/test_small_vector.cpp`
- Modify: `foundation/CMakeLists.txt`

- [ ] **Step 20.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.containers;

using namespace aleph::containers;

TEST_CASE("SmallVector<int, 4>: inline up to N, no heap allocations") {
    SmallVector<int, 4> v;
    for (int i = 0; i < 4; ++i) v.push_back(i);
    CHECK(v.size() == 4);
    CHECK(v.is_inline());
    for (int i = 0; i < 4; ++i) CHECK(v[i] == i);
}

TEST_CASE("SmallVector spills to heap past N") {
    SmallVector<int, 4> v;
    for (int i = 0; i < 10; ++i) v.push_back(i);
    CHECK(v.size() == 10);
    CHECK_FALSE(v.is_inline());
    for (int i = 0; i < 10; ++i) CHECK(v[i] == i);
}

TEST_CASE("SmallVector noexcept move + range-based for") {
    SmallVector<int, 4> a;
    for (int i = 0; i < 6; ++i) a.push_back(i);
    SmallVector<int, 4> b = std::move(a);
    int sum = 0;
    for (int x : b) sum += x;
    CHECK(sum == 15);
}
```

- [ ] **Step 20.2: Run to see fail.**

- [ ] **Step 20.3: Write `aleph.containers-small_vector.cppm`**

```cpp
module;
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <new>

export module aleph.containers:small_vector;

export namespace aleph::containers {

// Vector with N elements of inline storage; spills to heap (malloc/free)
// when capacity exceeds N. Trivially-copyable T only (no destructor calls
// in destroy path — keeps move/copy bookkeeping simple).
template<typename T, std::size_t N>
class SmallVector {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SmallVector requires trivially-copyable T");
public:
    SmallVector() noexcept = default;

    SmallVector(SmallVector&& o) noexcept { steal_from(o); }

    SmallVector& operator=(SmallVector&& o) noexcept {
        if (this != &o) { release(); steal_from(o); }
        return *this;
    }

    ~SmallVector() { release(); }

    void push_back(const T& v) noexcept {
        if (sz_ == cap_) grow();
        data_[sz_++] = v;
    }

    T&       operator[](std::size_t i)       noexcept { return data_[i]; }
    const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    std::size_t size()     const noexcept { return sz_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool        is_inline() const noexcept { return data_ == inline_storage(); }

    T*       begin()       noexcept { return data_; }
    T*       end()         noexcept { return data_ + sz_; }
    const T* begin() const noexcept { return data_; }
    const T* end()   const noexcept { return data_ + sz_; }

private:
    alignas(T) unsigned char inline_buf_[sizeof(T) * N]{};
    T*          data_{reinterpret_cast<T*>(inline_buf_)};
    std::size_t sz_{0};
    std::size_t cap_{N};

    T* inline_storage() noexcept { return reinterpret_cast<T*>(inline_buf_); }

    void grow() noexcept {
        const std::size_t new_cap = cap_ * 2;
        T* new_data = static_cast<T*>(std::malloc(sizeof(T) * new_cap));
        std::memcpy(new_data, data_, sizeof(T) * sz_);
        if (!is_inline()) std::free(data_);
        data_ = new_data;
        cap_  = new_cap;
    }

    void release() noexcept {
        if (!is_inline()) { std::free(data_); data_ = inline_storage(); }
        sz_ = 0;
        cap_ = N;
    }

    void steal_from(SmallVector& o) noexcept {
        if (o.is_inline()) {
            std::memcpy(inline_storage(), o.inline_storage(), sizeof(T) * o.sz_);
            data_ = inline_storage();
        } else {
            data_ = o.data_;
            o.data_ = o.inline_storage();
        }
        sz_  = o.sz_;
        cap_ = o.cap_;
        o.sz_  = 0;
        o.cap_ = N;
    }
};

}  // namespace aleph::containers
```

- [ ] **Step 20.4: Write primary unit + CMake**

`aleph.containers.cppm`:
```cpp
export module aleph.containers;
export import :small_vector;
```

`foundation/src/aleph.containers/CMakeLists.txt`:
```cmake
add_library(aleph_containers)
target_sources(aleph_containers
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.containers.cppm
        aleph.containers-small_vector.cppm)
target_link_libraries(aleph_containers
    PUBLIC  aleph_alloc
    PRIVATE aleph_flags_strict)
```

Append `add_subdirectory(src/aleph.containers)` to `foundation/CMakeLists.txt`. Append `aleph_containers` to test link libs and `containers/test_small_vector.cpp` to test sources.

- [ ] **Step 20.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.containers/ foundation/CMakeLists.txt tests/containers/test_small_vector.cpp tests/CMakeLists.txt
git commit -m "task 20: aleph.containers:small_vector — inline-N storage with heap spill"
```

---

## Task 21: `aleph.containers:flat_set` — sorted-vector set

**Files:**
- Create: `foundation/src/aleph.containers/aleph.containers-flat_set.cppm`
- Create: `tests/containers/test_flat_set.cpp`

- [ ] **Step 21.1: Write failing test**

```cpp
#include "doctest.h"
#include <functional>
import aleph.containers;

using namespace aleph::containers;

TEST_CASE("FlatSet inserts unique elements in sorted order") {
    FlatSet<int> s;
    s.insert(5);
    s.insert(2);
    s.insert(8);
    s.insert(2);   // dup
    CHECK(s.size() == 3);
    CHECK(s[0] == 2);
    CHECK(s[1] == 5);
    CHECK(s[2] == 8);
}

TEST_CASE("FlatSet::contains uses binary search") {
    FlatSet<int> s;
    for (int i = 0; i < 100; ++i) s.insert(i * 2);
    CHECK(s.contains(0));
    CHECK(s.contains(50));
    CHECK_FALSE(s.contains(51));
}
```

- [ ] **Step 21.2: Run to see fail.**

- [ ] **Step 21.3: Write `aleph.containers-flat_set.cppm`**

```cpp
module;
#include <algorithm>
#include <cstddef>
#include <vector>
#include <functional>

export module aleph.containers:flat_set;

export namespace aleph::containers {

template<typename K, typename Compare = std::less<K>>
class FlatSet {
public:
    void insert(const K& k) {
        auto it = std::lower_bound(data_.begin(), data_.end(), k, cmp_);
        if (it == data_.end() || cmp_(k, *it)) data_.insert(it, k);
    }

    [[nodiscard]] bool contains(const K& k) const noexcept {
        auto it = std::lower_bound(data_.begin(), data_.end(), k, cmp_);
        return it != data_.end() && !cmp_(k, *it);
    }

    [[nodiscard]] std::size_t size()  const noexcept { return data_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return data_.empty(); }

    const K& operator[](std::size_t i) const noexcept { return data_[i]; }

    auto begin() const noexcept { return data_.begin(); }
    auto end()   const noexcept { return data_.end();   }

private:
    std::vector<K> data_;
    Compare        cmp_{};
};

}  // namespace aleph::containers
```

- [ ] **Step 21.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.containers/aleph.containers-flat_set.cppm foundation/src/aleph.containers/aleph.containers.cppm foundation/src/aleph.containers/CMakeLists.txt tests/containers/test_flat_set.cpp tests/CMakeLists.txt
git commit -m "task 21: aleph.containers:flat_set — sorted-vector set, binary search lookup"
```

---

## Task 22: `aleph.containers:dense_index` — typed handle indexing

**Files:**
- Create: `foundation/src/aleph.containers/aleph.containers-dense_index.cppm`
- Create: `tests/containers/test_dense_index.cpp`

- [ ] **Step 22.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.containers;

using namespace aleph::containers;

struct NodeTag;
struct EdgeTag;

TEST_CASE("Handle<Tag>: typed integer ID, no implicit conversion") {
    using NodeId = Handle<NodeTag>;
    using EdgeId = Handle<EdgeTag>;
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);
    constexpr NodeId a{42}, b{42}, c{43};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("DenseIndex<NodeTag, std::string>: O(1) lookup, deterministic iteration") {
    DenseIndex<NodeTag, int> d;
    const auto id_a = d.push(10);
    const auto id_b = d.push(20);
    const auto id_c = d.push(30);

    CHECK(d.size() == 3);
    CHECK(d[id_a] == 10);
    CHECK(d[id_b] == 20);
    CHECK(d[id_c] == 30);

    int sum = 0;
    for (int v : d) sum += v;
    CHECK(sum == 60);
}
```

- [ ] **Step 22.2: Run to see fail.**

- [ ] **Step 22.3: Write `aleph.containers-dense_index.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>

export module aleph.containers:dense_index;

export namespace aleph::containers {

// Strongly-typed integer handle.
template<typename Tag>
struct Handle {
    std::uint32_t value{};
    friend constexpr bool operator==(Handle, Handle) = default;
};

// Vector with typed Handle<Tag> indexing. Replaces hash map for cases
// where stable insertion-order iteration is needed (= sustituye IndexMap).
template<typename Tag, typename T>
class DenseIndex {
public:
    Handle<Tag> push(const T& v) {
        const std::uint32_t id = static_cast<std::uint32_t>(data_.size());
        data_.push_back(v);
        return Handle<Tag>{id};
    }

    Handle<Tag> push(T&& v) {
        const std::uint32_t id = static_cast<std::uint32_t>(data_.size());
        data_.push_back(std::move(v));
        return Handle<Tag>{id};
    }

    T&       operator[](Handle<Tag> h)       noexcept { return data_[h.value]; }
    const T& operator[](Handle<Tag> h) const noexcept { return data_[h.value]; }

    std::size_t size()  const noexcept { return data_.size(); }
    bool        empty() const noexcept { return data_.empty(); }

    auto begin()       noexcept { return data_.begin(); }
    auto end()         noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end()   const noexcept { return data_.end(); }

private:
    std::vector<T> data_;
};

}  // namespace aleph::containers
```

- [ ] **Step 22.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.containers/aleph.containers-dense_index.cppm foundation/src/aleph.containers/aleph.containers.cppm foundation/src/aleph.containers/CMakeLists.txt tests/containers/test_dense_index.cpp tests/CMakeLists.txt
git commit -m "task 22: aleph.containers:dense_index — typed Handle<Tag> + DenseIndex<Tag, T>; containers module complete"
```

---

## Task 23: `aleph.threads:pool` — persistent jthread pool with parallel_for

**Files:**
- Create: `foundation/src/aleph.threads/CMakeLists.txt`
- Create: `foundation/src/aleph.threads/aleph.threads.cppm`
- Create: `foundation/src/aleph.threads/aleph.threads-pool.cppm`
- Create: `tests/threads/test_pool.cpp`
- Modify: `foundation/CMakeLists.txt`

- [ ] **Step 23.1: Write failing test**

```cpp
#include "doctest.h"
#include <atomic>
#include <vector>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("Pool: parallel_for runs each iteration exactly once") {
    Pool p(4);
    std::atomic<int> count{0};
    std::vector<int> seen(1000, 0);
    p.parallel_for(0, 1000, [&](int i) {
        seen[i] = 1;
        count.fetch_add(1, std::memory_order_relaxed);
    });
    CHECK(count.load() == 1000);
    for (int i = 0; i < 1000; ++i) CHECK(seen[i] == 1);
}

TEST_CASE("Pool: parallel_for over empty range is no-op") {
    Pool p(4);
    std::atomic<int> count{0};
    p.parallel_for(10, 10, [&](int) { count.fetch_add(1); });
    CHECK(count.load() == 0);
}

TEST_CASE("Pool: parallel_for n_threads=1 still works") {
    Pool p(1);
    std::atomic<int> sum{0};
    p.parallel_for(0, 100, [&](int i) { sum.fetch_add(i); });
    CHECK(sum.load() == 4950);   // sum(0..99)
}
```

- [ ] **Step 23.2: Run to see fail.**

- [ ] **Step 23.3: Write `aleph.threads-pool.cppm`**

```cpp
module;
#include <atomic>
#include <thread>
#include <vector>
#include <concepts>
#include <algorithm>

export module aleph.threads:pool;

export namespace aleph::threads {

// Persistent worker pool. No spawn-per-task. parallel_for uses dynamic
// chunks via std::atomic<int> counter — workers steal one index at a time.
//
// Design choice: keep the API minimal for foundation. submit/future and
// task DAGs can be added in a later phase if needed.
class Pool {
public:
    explicit Pool(int n) : n_{std::max(1, n)} {}

    // Run f(i) for each i in [begin, end), partitioned dynamically across
    // n threads. Blocks until all iterations complete.
    template<std::invocable<int> F>
    void parallel_for(int begin, int end, F f) {
        if (begin >= end) return;
        if (n_ == 1) {
            for (int i = begin; i < end; ++i) f(i);
            return;
        }
        std::atomic<int> next{begin};
        std::vector<std::jthread> workers;
        workers.reserve(static_cast<std::size_t>(n_));
        for (int t = 0; t < n_; ++t) {
            workers.emplace_back([&next, end, &f]() {
                for (;;) {
                    int i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= end) return;
                    f(i);
                }
            });
        }
        // jthreads join on destruction.
    }

    int n_threads() const noexcept { return n_; }

private:
    int n_;
};

}  // namespace aleph::threads
```

- [ ] **Step 23.4: Write primary unit + CMake**

`aleph.threads.cppm`:
```cpp
export module aleph.threads;
export import :pool;
```

`foundation/src/aleph.threads/CMakeLists.txt`:
```cmake
add_library(aleph_threads)
target_sources(aleph_threads
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.threads.cppm
        aleph.threads-pool.cppm)
find_package(Threads REQUIRED)
target_link_libraries(aleph_threads
    PUBLIC  aleph_alloc aleph_containers Threads::Threads
    PRIVATE aleph_flags_strict)
```

Append `add_subdirectory(src/aleph.threads)` to `foundation/CMakeLists.txt`. Append `aleph_threads` to test link libs and `threads/test_pool.cpp` to test sources.

- [ ] **Step 23.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.threads/ foundation/CMakeLists.txt tests/threads/test_pool.cpp tests/CMakeLists.txt
git commit -m "task 23: aleph.threads:pool — persistent jthread pool with parallel_for"
```

---

## Task 24: `aleph.threads:mpmc` — Vyukov bounded MPMC ring buffer

**Files:**
- Create: `foundation/src/aleph.threads/aleph.threads-mpmc.cppm`
- Create: `tests/threads/test_mpmc.cpp`

- [ ] **Step 24.1: Write failing test**

```cpp
#include "doctest.h"
#include <atomic>
#include <thread>
#include <vector>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("MpmcRing: single-thread push/pop preserves FIFO") {
    MpmcRing<int, 16> q;
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    int v;
    CHECK(q.try_pop(v)); CHECK(v == 1);
    CHECK(q.try_pop(v)); CHECK(v == 2);
    CHECK(q.try_pop(v)); CHECK(v == 3);
    CHECK_FALSE(q.try_pop(v));
}

TEST_CASE("MpmcRing: full ring rejects push") {
    MpmcRing<int, 4> q;
    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    CHECK(q.try_push(4));
    CHECK_FALSE(q.try_push(5));
}

TEST_CASE("MpmcRing: 2 producers + 2 consumers, all items received once") {
    MpmcRing<int, 1024> q;
    constexpr int per = 5000;
    std::atomic<int> received_sum{0};
    std::atomic<int> received_n{0};

    auto producer = [&](int base) {
        for (int i = 0; i < per; ++i) {
            while (!q.try_push(base + i)) std::this_thread::yield();
        }
    };
    auto consumer = [&]() {
        int v;
        while (received_n.load() < 2 * per) {
            if (q.try_pop(v)) {
                received_sum.fetch_add(v);
                received_n.fetch_add(1);
            }
        }
    };

    std::jthread p1(producer, 0);
    std::jthread p2(producer, per);
    std::jthread c1(consumer);
    std::jthread c2(consumer);
    p1.join(); p2.join(); c1.join(); c2.join();

    // sum(0..2*per-1) = (2*per-1)*per
    CHECK(received_sum.load() == (2 * per - 1) * per);
    CHECK(received_n.load() == 2 * per);
}
```

- [ ] **Step 24.2: Run to see fail.**

- [ ] **Step 24.3: Write `aleph.threads-mpmc.cppm`**

```cpp
module;
#include <atomic>
#include <cstddef>
#include <cstdint>

export module aleph.threads:mpmc;

export namespace aleph::threads {

// Vyukov bounded MPMC ring. Capacity must be a power of two ≥ 2.
// Wait-free for uncontended pushes/pops; spin-loop on contention.
template<typename T, std::size_t Capacity>
class MpmcRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
public:
    MpmcRing() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i)
            cells_[i].seq.store(i, std::memory_order_relaxed);
    }

    [[nodiscard]] bool try_push(const T& v) noexcept {
        Cell* cell;
        std::size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & MASK];
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)pos;
            if (diff == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false;   // full
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
        cell->data = v;
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        Cell* cell;
        std::size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & MASK];
            std::size_t seq = cell->seq.load(std::memory_order_acquire);
            std::intptr_t diff = (std::intptr_t)seq - (std::intptr_t)(pos + 1);
            if (diff == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false;   // empty
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
        out = cell->data;
        cell->seq.store(pos + MASK + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t MASK = Capacity - 1;
    struct alignas(64) Cell {
        std::atomic<std::size_t> seq;
        T                        data;
    };
    alignas(64) Cell cells_[Capacity];
    alignas(64) std::atomic<std::size_t> enq_{0};
    alignas(64) std::atomic<std::size_t> deq_{0};
};

}  // namespace aleph::threads
```

- [ ] **Step 24.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.threads/aleph.threads-mpmc.cppm foundation/src/aleph.threads/aleph.threads.cppm foundation/src/aleph.threads/CMakeLists.txt tests/threads/test_mpmc.cpp tests/CMakeLists.txt
git commit -m "task 24: aleph.threads:mpmc — Vyukov bounded MPMC ring buffer"
```

---

## Task 25: `aleph.threads:work_stealing` — Chase-Lev deque

**Files:**
- Create: `foundation/src/aleph.threads/aleph.threads-work_stealing.cppm`
- Create: `tests/threads/test_work_stealing.cpp`

- [ ] **Step 25.1: Write failing test**

```cpp
#include "doctest.h"
#include <atomic>
#include <thread>
import aleph.threads;

using namespace aleph::threads;

TEST_CASE("WorkStealingDeque<int>: owner push/pop FIFO from same end") {
    WorkStealingDeque<int> q;
    q.push(10);
    q.push(20);
    q.push(30);
    int v;
    CHECK(q.pop(v)); CHECK(v == 30);   // LIFO from owner end
    CHECK(q.pop(v)); CHECK(v == 20);
    CHECK(q.pop(v)); CHECK(v == 10);
    CHECK_FALSE(q.pop(v));
}

TEST_CASE("WorkStealingDeque: thieves steal from far end") {
    WorkStealingDeque<int> q;
    for (int i = 0; i < 100; ++i) q.push(i);

    std::atomic<int> stolen{0};
    std::atomic<int> sum{0};
    auto thief = [&] {
        int v;
        for (;;) {
            if (q.steal(v)) {
                stolen.fetch_add(1);
                sum.fetch_add(v);
            } else if (q.empty()) {
                break;
            }
        }
    };
    std::jthread t1(thief), t2(thief);
    t1.join(); t2.join();
    CHECK(stolen.load() == 100);
    CHECK(sum.load() == 99 * 100 / 2);
}
```

- [ ] **Step 25.2: Run to see fail.**

- [ ] **Step 25.3: Write `aleph.threads-work_stealing.cppm`**

```cpp
module;
#include <atomic>
#include <vector>
#include <cstdint>

export module aleph.threads:work_stealing;

export namespace aleph::threads {

// Chase-Lev work-stealing deque. Owner thread pushes/pops at the
// "bottom"; thieves steal from the "top". Lock-free.
//
// Storage grows by doubling. T must be trivially-copyable for simplicity
// (sufficient for handle/index payloads used in BVH build).
template<typename T>
class WorkStealingDeque {
public:
    WorkStealingDeque() { buf_.resize(initial_cap); }

    void push(const T& v) {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed);
        const std::int64_t t = top_.load(std::memory_order_acquire);
        if (b - t >= static_cast<std::int64_t>(buf_.size())) {
            std::vector<T> grown(buf_.size() * 2);
            for (std::int64_t i = t; i < b; ++i)
                grown[i & (grown.size() - 1)] = buf_[i & (buf_.size() - 1)];
            buf_ = std::move(grown);
        }
        buf_[b & (buf_.size() - 1)] = v;
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    // Owner pop — LIFO.
    bool pop(T& out) {
        const std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::int64_t t = top_.load(std::memory_order_relaxed);
        if (t <= b) {
            out = buf_[b & (buf_.size() - 1)];
            if (t == b) {
                if (!top_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    bottom_.store(b + 1, std::memory_order_relaxed);
                    return false;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            return true;
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
        return false;
    }

    // Thief steal — FIFO from the other end.
    bool steal(T& out) {
        std::int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t b = bottom_.load(std::memory_order_acquire);
        if (t < b) {
            out = buf_[t & (buf_.size() - 1)];
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return false;
            }
            return true;
        }
        return false;
    }

    bool empty() const noexcept {
        return bottom_.load(std::memory_order_acquire) <=
               top_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t initial_cap = 64;
    std::vector<T> buf_;
    alignas(64) std::atomic<std::int64_t> top_{0};
    alignas(64) std::atomic<std::int64_t> bottom_{0};
};

}  // namespace aleph::threads
```

- [ ] **Step 25.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.threads/aleph.threads-work_stealing.cppm foundation/src/aleph.threads/aleph.threads.cppm foundation/src/aleph.threads/CMakeLists.txt tests/threads/test_work_stealing.cpp tests/CMakeLists.txt
git commit -m "task 25: aleph.threads:work_stealing — Chase-Lev deque; threads module complete"
```

---

## Task 26: `aleph.io:mmap` — RAII mmap wrapper

**Files:**
- Create: `foundation/src/aleph.io/CMakeLists.txt`
- Create: `foundation/src/aleph.io/aleph.io.cppm`
- Create: `foundation/src/aleph.io/aleph.io-mmap.cppm`
- Create: `tests/io/test_mmap.cpp`
- Modify: `foundation/CMakeLists.txt`

- [ ] **Step 26.1: Write failing test**

```cpp
#include "doctest.h"
#include <cstdio>
#include <filesystem>
#include <string>
import aleph.io;

using namespace aleph::io;

TEST_CASE("MappedFile: open existing, expose bytes()") {
    const auto path = (std::filesystem::temp_directory_path()
                       / "aleph_test_mmap.bin").string();
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        const char payload[] = "abc123";
        std::fwrite(payload, 1, sizeof(payload) - 1, f);
        std::fclose(f);
    }

    auto r = MappedFile::open_read(path);
    REQUIRE(r);
    const auto bytes = r->bytes();
    CHECK(bytes.size() == 6);
    CHECK(static_cast<char>(bytes[0]) == 'a');
    CHECK(static_cast<char>(bytes[5]) == '3');

    std::remove(path.c_str());
}

TEST_CASE("MappedFile: error on missing path") {
    auto r = MappedFile::open_read("/definitely/does/not/exist.bin");
    CHECK_FALSE(r);
}
```

- [ ] **Step 26.2: Run to see fail.**

- [ ] **Step 26.3: Write `aleph.io-mmap.cppm`**

```cpp
module;
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <expected>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

export module aleph.io:mmap;

export namespace aleph::io {

class MappedFile {
public:
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept
        : data_{o.data_}, size_{o.size_} { o.data_ = nullptr; o.size_ = 0; }
    MappedFile& operator=(MappedFile&& o) noexcept {
        if (this != &o) { release(); data_ = o.data_; size_ = o.size_;
                           o.data_ = nullptr; o.size_ = 0; }
        return *this;
    }
    ~MappedFile() { release(); }

    static std::expected<MappedFile, std::string>
    open_read(std::string_view path) noexcept {
        std::string p{path};
        const int fd = ::open(p.c_str(), O_RDONLY);
        if (fd < 0) return std::unexpected("open failed: " + p);
        struct stat st{};
        if (::fstat(fd, &st) < 0) { ::close(fd);
                                     return std::unexpected("fstat failed: " + p); }
        const std::size_t sz = static_cast<std::size_t>(st.st_size);
        void* map = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED) return std::unexpected("mmap failed: " + p);
        MappedFile mf;
        mf.data_ = static_cast<const std::byte*>(map);
        mf.size_ = sz;
        return mf;
    }

    std::span<const std::byte> bytes() const noexcept { return {data_, size_}; }

private:
    void release() noexcept {
        if (data_) { ::munmap(const_cast<std::byte*>(data_), size_); data_ = nullptr; size_ = 0; }
    }
    const std::byte* data_{nullptr};
    std::size_t      size_{0};
};

}  // namespace aleph::io
```

- [ ] **Step 26.4: Wire up**

`aleph.io.cppm`:
```cpp
export module aleph.io;
export import :mmap;
```

`foundation/src/aleph.io/CMakeLists.txt`:
```cmake
add_library(aleph_io)
target_sources(aleph_io
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.io.cppm
        aleph.io-mmap.cppm)
target_link_libraries(aleph_io
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_strict)
```

Append `add_subdirectory(src/aleph.io)` to `foundation/CMakeLists.txt`. Append `aleph_io` to test link libs and `io/test_mmap.cpp` to test sources.

- [ ] **Step 26.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.io/ foundation/CMakeLists.txt tests/io/test_mmap.cpp tests/CMakeLists.txt
git commit -m "task 26: aleph.io:mmap — RAII MappedFile (PROT_READ MAP_PRIVATE)"
```

---

## Task 27: `aleph.io:ppm` — load PPM (P6) from byte span

**Files:**
- Create: `foundation/src/aleph.io/aleph.io-ppm.cppm`
- Create: `tests/io/test_ppm.cpp`

- [ ] **Step 27.1: Write failing test**

```cpp
#include "doctest.h"
#include <cstdio>
#include <filesystem>
import aleph.io;

using namespace aleph::io;

TEST_CASE("load_ppm parses a valid 2x1 P6 image") {
    // Build a tiny PPM in memory: 2x1 image, pixels (255,0,0) and (0,255,0).
    const unsigned char bytes[] = {
        'P','6','\n','2',' ','1','\n','2','5','5','\n',
        255, 0, 0,   0, 255, 0
    };
    auto r = load_ppm(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(bytes), sizeof(bytes)});
    REQUIRE(r);
    CHECK(r->width  == 2);
    CHECK(r->height == 1);
    CHECK(r->pixels.size() == 6);
    CHECK(r->pixels[0] == std::byte{255});
    CHECK(r->pixels[4] == std::byte{255});
}

TEST_CASE("load_ppm rejects a non-P6 magic") {
    const unsigned char bytes[] = {'P','3','\n','1',' ','1','\n','2','5','5','\n', 0,0,0};
    auto r = load_ppm(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(bytes), sizeof(bytes)});
    CHECK_FALSE(r);
}
```

- [ ] **Step 27.2: Run to see fail.**

- [ ] **Step 27.3: Write `aleph.io-ppm.cppm`**

```cpp
module;
#include <cstddef>
#include <span>
#include <vector>
#include <string>
#include <expected>
#include <cstring>
#include <charconv>

export module aleph.io:ppm;

export namespace aleph::io {

struct Image {
    int                    width{0};
    int                    height{0};
    std::vector<std::byte> pixels;   // RGB8 interleaved, row-major
};

namespace detail {

// Skip a single whitespace + comments. Returns updated index.
inline std::size_t skip_ws(std::span<const std::byte> b, std::size_t i) {
    while (i < b.size()) {
        char c = static_cast<char>(b[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
        if (c == '#') { while (i < b.size() && static_cast<char>(b[i]) != '\n') ++i; continue; }
        return i;
    }
    return i;
}

// Parse a non-negative integer from b starting at i, returning {value, end}.
inline std::expected<std::pair<int, std::size_t>, std::string>
parse_int(std::span<const std::byte> b, std::size_t i) {
    std::size_t j = i;
    while (j < b.size()) {
        char c = static_cast<char>(b[j]);
        if (c < '0' || c > '9') break;
        ++j;
    }
    if (i == j) return std::unexpected("expected integer");
    int v = 0;
    auto [_, ec] = std::from_chars(
        reinterpret_cast<const char*>(b.data()) + i,
        reinterpret_cast<const char*>(b.data()) + j, v);
    if (ec != std::errc{}) return std::unexpected("integer parse error");
    return std::pair{v, j};
}

}  // namespace detail

inline std::expected<Image, std::string>
load_ppm(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 3 ||
        static_cast<char>(bytes[0]) != 'P' || static_cast<char>(bytes[1]) != '6')
        return std::unexpected("PPM: expected P6 magic");

    std::size_t i = 2;
    i = detail::skip_ws(bytes, i);
    auto w = detail::parse_int(bytes, i);
    if (!w) return std::unexpected(w.error());
    i = detail::skip_ws(bytes, w->second);

    auto h = detail::parse_int(bytes, i);
    if (!h) return std::unexpected(h.error());
    i = detail::skip_ws(bytes, h->second);

    auto m = detail::parse_int(bytes, i);
    if (!m) return std::unexpected(m.error());
    if (m->first != 255) return std::unexpected("PPM: maxval must be 255");
    i = m->second;
    // Exactly one whitespace after maxval (PPM convention).
    if (i >= bytes.size()) return std::unexpected("PPM: truncated");
    ++i;

    const std::size_t n = static_cast<std::size_t>(w->first) *
                          static_cast<std::size_t>(h->first) * 3u;
    if (bytes.size() - i < n) return std::unexpected("PPM: truncated pixel data");

    Image img;
    img.width  = w->first;
    img.height = h->first;
    img.pixels.assign(bytes.begin() + i, bytes.begin() + i + n);
    return img;
}

}  // namespace aleph::io
```

- [ ] **Step 27.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.io/aleph.io-ppm.cppm foundation/src/aleph.io/aleph.io.cppm foundation/src/aleph.io/CMakeLists.txt tests/io/test_ppm.cpp tests/CMakeLists.txt
git commit -m "task 27: aleph.io:ppm — zero-copy load_ppm from byte span"
```

---

## Task 28: `aleph.io:obj` — load Wavefront OBJ (positions + tri indices)

**Files:**
- Create: `foundation/src/aleph.io/aleph.io-obj.cppm`
- Create: `tests/io/test_obj.cpp`

- [ ] **Step 28.1: Write failing test**

```cpp
#include "doctest.h"
#include <cstring>
import aleph.io;

using namespace aleph::io;

TEST_CASE("load_obj: parses verts and faces") {
    const char data[] =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 0 1\n"
        "f 1 2 3\n"
        "f 1 3 4\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1});
    REQUIRE(r);
    CHECK(r->verts.size() == 4);
    CHECK(r->tris.size()  == 2);
    CHECK(r->tris[0][0] == 0);
    CHECK(r->tris[0][1] == 1);
    CHECK(r->tris[0][2] == 2);
}

TEST_CASE("load_obj: ignores comments and unknown directives") {
    const char data[] =
        "# this is a comment\n"
        "vn 0 1 0\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    auto r = load_obj(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(data), sizeof(data) - 1});
    REQUIRE(r);
    CHECK(r->verts.size() == 3);
    CHECK(r->tris.size()  == 1);
}
```

- [ ] **Step 28.2: Run to see fail.**

- [ ] **Step 28.3: Write `aleph.io-obj.cppm`**

```cpp
module;
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>

export module aleph.io:obj;

export namespace aleph::io {

struct Vec3f { float x{}, y{}, z{}; };

struct ObjMesh {
    std::vector<Vec3f>              verts;
    std::vector<std::array<int, 3>> tris;
};

inline std::expected<ObjMesh, std::string>
load_obj(std::span<const std::byte> bytes) noexcept {
    ObjMesh mesh;
    std::size_t i = 0;
    while (i < bytes.size()) {
        std::size_t j = i;
        while (j < bytes.size() && static_cast<char>(bytes[j]) != '\n') ++j;
        std::string_view line{
            reinterpret_cast<const char*>(bytes.data()) + i, j - i};
        i = j + 1;

        if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
            Vec3f v;
            if (std::sscanf(line.data(), "v %f %f %f", &v.x, &v.y, &v.z) == 3)
                mesh.verts.push_back(v);
        } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
            std::array<int, 16> idx{};
            int n_idx = 0;
            const char* p = line.data() + 2;
            const char* end = line.data() + line.size();
            while (p < end && n_idx < 16) {
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p >= end) break;
                int v = 0;
                if (std::sscanf(p, "%d", &v) != 1) break;
                if (v < 0) v = static_cast<int>(mesh.verts.size()) + v + 1;
                idx[n_idx++] = v - 1;
                while (p < end && *p != ' ' && *p != '\t') ++p;
            }
            for (int k = 1; k < n_idx - 1; ++k) {
                const int a = idx[0], b = idx[k], c = idx[k + 1];
                const int n = static_cast<int>(mesh.verts.size());
                if (a < 0 || a >= n || b < 0 || b >= n || c < 0 || c >= n) continue;
                mesh.tris.push_back({a, b, c});
            }
        }
    }
    return mesh;
}

}  // namespace aleph::io
```

- [ ] **Step 28.4: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
git add foundation/src/aleph.io/aleph.io-obj.cppm foundation/src/aleph.io/aleph.io.cppm foundation/src/aleph.io/CMakeLists.txt tests/io/test_obj.cpp tests/CMakeLists.txt
git commit -m "task 28: aleph.io:obj — Wavefront OBJ loader (positions + fan-triangulated faces); io module complete"
```

---

## Task 29: Module-graph isolation enforcement

Verify each module compiles in isolation against its declared dependencies only. Catches accidental cross-module imports (e.g., `aleph.math` reaching into `aleph.threads`).

**Files:**
- Create: `tests/isolation/CMakeLists.txt`
- Create: `tests/isolation/iso_cpu.cpp`
- Create: `tests/isolation/iso_math.cpp`
- Create: `tests/isolation/iso_alloc.cpp`
- Create: `tests/isolation/iso_containers.cpp`
- Create: `tests/isolation/iso_io.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 29.1: Write each isolation source**

`tests/isolation/iso_cpu.cpp`:
```cpp
import aleph.cpu;
int main() { aleph::cpu::assert_isa_compatible(); return 0; }
```

`tests/isolation/iso_math.cpp`:
```cpp
import aleph.math;
int main() {
    aleph::math::Vec3 v{1, 2, 3};
    return static_cast<int>(dot(v, v)) - 14;   // returns 0 if math is intact
}
```

`tests/isolation/iso_alloc.cpp`:
```cpp
import aleph.alloc;
int main() {
    alignas(16) static unsigned char buf[1024];
    aleph::alloc::Arena a{buf, sizeof(buf)};
    return a.allocate(16, 16) ? 0 : 1;
}
```

`tests/isolation/iso_containers.cpp`:
```cpp
import aleph.containers;
int main() {
    aleph::containers::SmallVector<int, 4> v;
    v.push_back(7);
    return v[0] - 7;
}
```

`tests/isolation/iso_io.cpp`:
```cpp
import aleph.io;
int main() {
    auto r = aleph::io::MappedFile::open_read("/does/not/exist");
    return r.has_value() ? 1 : 0;
}
```

- [ ] **Step 29.2: Write `tests/isolation/CMakeLists.txt`**

```cmake
# Each iso_X.cpp is built into its own tiny executable that imports
# exactly ONE module. If a future change makes that module pull in a
# cross-module dep that isn't declared in CMake, this target fails to
# link. CTest verifies they all run cleanly.

function(aleph_iso_test name link)
    add_executable(iso_${name} iso_${name}.cpp)
    target_link_libraries(iso_${name} PRIVATE ${link} aleph_flags_test)
    add_test(NAME iso_${name} COMMAND iso_${name})
endfunction()

aleph_iso_test(cpu        aleph_cpu)
aleph_iso_test(math       aleph_math)
aleph_iso_test(alloc      aleph_alloc)
aleph_iso_test(containers aleph_containers)
aleph_iso_test(io         aleph_io)
```

Append `add_subdirectory(isolation)` to `tests/CMakeLists.txt`.

- [ ] **Step 29.3: Build + run all tests**

```bash
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Expected: `6/6 tests passed` (`aleph_tests` plus 5 iso_*).

- [ ] **Step 29.4: Commit**

```bash
git add tests/isolation/ tests/CMakeLists.txt
git commit -m "task 29: per-module isolation tests — enforce no accidental cross-module deps"
```

---

## Task 30: Bench harness — `aleph_bench`

**Files:**
- Create: `bench/CMakeLists.txt`
- Create: `bench/bench_harness.hpp`
- Create: `bench/bench_main.cpp`
- Modify: top-level `CMakeLists.txt` (add `add_subdirectory(bench)`)

- [ ] **Step 30.1: Write `bench/bench_harness.hpp`**

```cpp
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

import aleph.cpu;

namespace aleph_bench {

// Run `f(iters)` repeatedly with growing iter counts, measure cycles
// per iteration, report the median across `n_samples`.
//
// `f` must compute something that touches `iters` and return a value
// (used as a sink to prevent the optimizer from deleting the loop).
template<typename F>
void bench(std::string_view name, F&& f, int n_samples = 50, std::uint64_t iters = 100000) {
    std::vector<double> cycles_per_op;
    cycles_per_op.reserve(static_cast<std::size_t>(n_samples));

    // Warm-up to populate caches and pin the CPU governor.
    for (int w = 0; w < 5; ++w) (void)f(iters);

    for (int s = 0; s < n_samples; ++s) {
        const auto t0 = aleph::cpu::rdtscp();
        auto sink     = f(iters);
        const auto t1 = aleph::cpu::rdtscp();
        // Force `sink` to be live so the compiler doesn't elide the work.
        asm volatile("" :: "r"(&sink) : "memory");
        const double cyc = static_cast<double>(t1 - t0) / static_cast<double>(iters);
        cycles_per_op.push_back(cyc);
    }
    std::sort(cycles_per_op.begin(), cycles_per_op.end());
    const double median = cycles_per_op[static_cast<std::size_t>(n_samples) / 2];
    std::printf("  %-40.40s  %7.2f cyc/op  (median of %d samples)\n",
                std::string{name}.c_str(), median, n_samples);
}

}  // namespace aleph_bench
```

- [ ] **Step 30.2: Write `bench/bench_main.cpp` skeleton**

```cpp
#include "bench_harness.hpp"
#include <cstdio>

int main() {
    aleph::cpu::assert_isa_compatible();
    std::printf("aleph-cxx foundation benchmarks (x86-64-v3, AVX2 + FMA)\n");
    std::printf("------------------------------------------------------\n");
    // Benchmark cases added in Task 31.
    return 0;
}
```

- [ ] **Step 30.3: Write `bench/CMakeLists.txt`**

```cmake
add_executable(aleph_bench
    bench_main.cpp)
target_include_directories(aleph_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aleph_bench PRIVATE
    aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads
    aleph_flags_test)
```

- [ ] **Step 30.4: Wire + build**

Append `add_subdirectory(bench)` to top-level `CMakeLists.txt`.

```bash
cmake --build build-release --target aleph_bench
./build-release/bench/aleph_bench
```

Expected: prints header + table (empty body for now).

- [ ] **Step 30.5: Commit**

```bash
git add bench/ CMakeLists.txt
git commit -m "task 30: bench harness — rdtscp-based microbench, median over N samples"
```

---

## Task 31: Day-1 baseline benchmarks + success-criteria validation

Fill in the bench cases for the six day-1 baselines from the spec, and add a script that asserts each meets its target.

**Files:**
- Modify: `bench/bench_main.cpp`
- Create: `bench/run-baselines.sh`

- [ ] **Step 31.1: Extend `bench/bench_main.cpp` with the day-1 cases**

```cpp
#include "bench_harness.hpp"
#include <cstdio>
#include <cstdint>

import aleph.math;
import aleph.alloc;
import aleph.threads;

using aleph::math::Vec3;
using aleph::math::Vec4;
using aleph::math::Mat4;
using aleph::math::Rotor;
using aleph::math::from_axis_angle;

int main() {
    aleph::cpu::assert_isa_compatible();
    std::printf("aleph-cxx foundation benchmarks (x86-64-v3, AVX2 + FMA)\n");
    std::printf("------------------------------------------------------\n");

    // Rotor compose — target ≤ 6 cycles
    {
        Rotor a = from_axis_angle({1, 0, 0}, 0.3f);
        const Rotor b = from_axis_angle({0, 1, 0}, 0.2f);
        aleph_bench::bench("Rotor compose", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) a = a * b;
            return a;
        });
    }

    // Vec3 dot — target ≤ 3 cycles
    {
        Vec3 a{1, 2, 3};
        const Vec3 b{4, 5, 6};
        aleph_bench::bench("Vec3 dot", [&](std::uint64_t iters) {
            float s = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                s += dot(a, b);
                a.x = s * 1e-6f;
            }
            return s;
        });
    }

    // Vec3 add — target ≤ 3 cycles
    {
        Vec3 a{0, 0, 0};
        const Vec3 b{1, 1, 1};
        aleph_bench::bench("Vec3 add", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) a = a + b;
            return a;
        });
    }

    // Mat4 * Vec4 — target ≤ 8 cycles
    {
        const Mat4 M = Mat4::perspective(1.0f, 16.0f/9.0f, 0.1f, 100.0f);
        Vec4 v{1, 2, 3, 1};
        aleph_bench::bench("Mat4 * Vec4", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) v = M * v;
            return v;
        });
    }

    // Arena allocate(64) — target ≤ 3 cycles
    {
        alignas(64) static unsigned char buf[1 << 20];
        aleph::alloc::Arena arena{buf, sizeof(buf)};
        aleph_bench::bench("Arena allocate(64)", [&](std::uint64_t iters) {
            std::uintptr_t sink = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                if (arena.bytes_in_use() + 64 > arena.capacity()) arena.reset();
                sink ^= reinterpret_cast<std::uintptr_t>(arena.allocate(64, 16));
            }
            return sink;
        });
    }

    // MpmcRing<u64,1024> uncontended push+pop — target ≤ 10 ns ≈ ~40 cyc @ 4 GHz
    {
        aleph::threads::MpmcRing<std::uint64_t, 1024> q;
        aleph_bench::bench("MpmcRing<u64,1024> push+pop", [&](std::uint64_t iters) {
            std::uint64_t out = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                q.try_push(i);
                q.try_pop(out);
            }
            return out;
        });
    }

    return 0;
}
```

- [ ] **Step 31.2: Build + run**

```bash
cmake --preset bench
cmake --build build-bench --target aleph_bench
./build-bench/bench/aleph_bench
```

Expected: six lines printing `… cyc/op`. Verify each baseline meets its target manually first (numbers may need tuning for your kernel; if any baseline blows past target by 2× or more, investigate before scripting the check).

- [ ] **Step 31.3: Write `bench/run-baselines.sh`**

```bash
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
    # Extract cycles/op number for this name.
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
```

Make executable: `chmod +x bench/run-baselines.sh`.

- [ ] **Step 31.4: Run the baseline script**

```bash
./bench/run-baselines.sh
```
Expected: each line prints `OK …`, exit 0.

If any baseline `FAIL`s, follow the kernel back (check `objdump -d`, ensure `-O3 -mavx2 -mfma` reached the function, look for missed inlining). Don't accept a baseline that's > 2× the target without root-cause; if the target itself was wrong, update both spec and script in one commit.

- [ ] **Step 31.5: Commit**

```bash
git add bench/bench_main.cpp bench/run-baselines.sh
git commit -m "task 31: day-1 baselines + run-baselines.sh — Rotor/Vec/Mat/Arena/MPMC under targets"
```

---

## Task 32: Final validation pass + asan run

Closes the foundation phase. Walks each success criterion from the spec and proves it.

- [ ] **Step 32.1: Verify success criterion 1 — release-strict builds with zero warnings**

```bash
cd /home/lkz/aleph-cxx
rm -rf build-release-strict
cmake --preset release-strict
cmake --build build-release-strict 2>&1 | tee /tmp/aleph-strict-build.log
# Expect no `warning:` lines in the log; tests are excluded under this preset.
! grep -E "warning:" /tmp/aleph-strict-build.log
```

- [ ] **Step 32.2: Verify success criterion 2 — all tests pass under release**

```bash
cmake --build build-release
ctest --test-dir build-release --output-on-failure
# Expect: 6/6 (aleph_tests + 5 iso_*) passed.
```

- [ ] **Step 32.3: Verify success criterion 3 — baselines hit targets**

```bash
cmake --build build-bench --target aleph_bench
./bench/run-baselines.sh
```

- [ ] **Step 32.4: Verify success criterion 4 — assert_isa_compatible aborts on non-AVX2**

(Requires qemu-system-x86 or qemu-user. Skip if qemu absent and document in commit.)

```bash
qemu-x86_64 -cpu nehalem ./build-release/tests/iso_cpu 2>&1 | head -5 || true
# Expect a message about missing ISA features and a non-zero exit.
```

- [ ] **Step 32.5: Verify success criterion 5 — asan + ubsan run clean**

```bash
cmake --preset asan
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
# Expect 6/6 passed with no asan/ubsan diagnostics.
```

- [ ] **Step 32.6: Verify success criterion 6 — module graph has no cycles**

```bash
ctest --test-dir build-release -R '^iso_' --output-on-failure
# All 5 iso_* tests pass (each linking against exactly one aleph_* library).
```

- [ ] **Step 32.7: Tag the release**

```bash
git tag -a v0.1.0-foundation -m "aleph-cxx Phase 1 (foundation) complete: 6 modules, doctest + bench green"
git log --oneline | head -10
```

- [ ] **Step 32.8: Push the branch + tag**

```bash
# Only run after the user agrees — pushing is visible to others.
git push -u origin main
git push origin v0.1.0-foundation
```

---

## Notes for the executing engineer

- **Order matters.** Tasks 1–28 build the library; 29 enforces module isolation; 30–31 add bench infrastructure and validate cycle targets; 32 is the final gate. Run them in order — the dependency graph is `cpu → {math, alloc, io} → containers → threads`, and tasks reflect that.
- **Each task is self-contained.** If a step fails (e.g., GCC ICE on a particular module syntax), stop and triage before moving forward. Do NOT skip the failing test and come back to it — the next task will rely on the API you just wrote.
- **Modules + GCC 16 caveats.** Sotark cxx26 already hit a GCC ICE on `friend operator== = default` inside an imported module. We avoided that pattern in this plan (manual `operator==` only). If you hit a new ICE, isolate the offending syntax and prefer the older form (member function, no defaults).
- **`-fno-exceptions` in release-strict.** All foundation code is `noexcept`. If you find yourself wanting to `throw`, the design wants you to return `std::expected` instead. Test code lives under `release` preset where exceptions are on — doctest needs them.
- **No allocations on hot paths.** `Arena`, `Frame`, `Slab`, `FreeList` exist precisely to make this measurable. If a profile shows `malloc` in a render-time call stack, that's a bug.
- **Determinism.** No `std::unordered_*` anywhere. If you need a map, use `FlatSet<pair>` or `DenseIndex<Tag, T>`.

## Spec coverage check

| Spec requirement                                     | Tasks                  |
| ---                                                  | ---                    |
| Repo layout + `/home/lkz/aleph-cxx/`                 | 1                      |
| CMake 4.3 + Ninja + 5 presets                        | 1                      |
| doctest vendored                                     | 2                      |
| `aleph.cpu` module (CPUID, rdtsc, prefetch, hints)   | 3                      |
| `aleph.math` types                                   | 4                      |
| `aleph.math` Vec/Mat/Quat                            | 5, 6, 7                |
| `aleph.math` Rotor + GA G(3,0,0)                     | 8, 9, 10, 11           |
| Dual numbers (autodiff)                              | 12                     |
| `TangentVec<Manifold>` tag-distinct                  | 13                     |
| AABB, Ray, Plane                                     | 14                     |
| `aleph.alloc` Arena/Frame/Slab/FreeList/pmr_adapter  | 15, 16, 17, 18, 19     |
| `aleph.containers` SmallVector/FlatSet/DenseIndex    | 20, 21, 22             |
| `aleph.threads` Pool/MPMC/WorkStealing               | 23, 24, 25             |
| `aleph.io` MappedFile/PPM/OBJ                        | 26, 27, 28             |
| Module-graph isolation enforcement                   | 29                     |
| Bench harness                                        | 30                     |
| Day-1 cycle-target baselines                         | 31                     |
| All 6 success criteria from spec                     | 32                     |

