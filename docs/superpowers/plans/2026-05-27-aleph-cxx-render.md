# aleph-cxx Scene & Render (Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build 6 new C++26 module libraries on top of the Phase 1 foundation — `aleph.scene` (SoA storage + unified BVH), `aleph.render.common` (Camera/Film/tonemap/Sky), `aleph.render.rt` (path tracer + NEE), `aleph.render.sw` (rasterizer + span buffer + lightmap), `aleph.window` (SDL2 wrapper), `aleph.editor` (orbit cam + picking + UI) — plus two new executables (`aleph_rt` CLI raytracer, `aleph_sw` SDL editor), achieving Sotark cxx26 functional parity on a SoA-per-kind + `Handle32{kind:8, idx:24}` data layout.

**Architecture:** SoA stores per hittable/material kind (no `std::variant` in hot paths). One unified BVH whose leaves carry `Handle32` (8-bit kind tag + 24-bit dense index). One branch per visited leaf (kind switch). Renderers run on `aleph.threads::Pool` (rt: tiles, sw: horizontal stripes). Only `aleph.window` links SDL2; the rest of the engine stays SDL-agnostic. Apps live in a new top-level `apps/` directory.

**Tech Stack:** GCC 16.1.1 `-std=c++26` C++20+ modules, CMake 4.3 + Ninja, doctest 2.4.11 (vendored), SDL2 (sdl2-compat-devel — already installed). Foundation modules `aleph.cpu/.math/.alloc/.containers/.threads/.io` from Phase 1 (`v0.1.1-foundation`).

---

## Task 1: Branch setup + render/ + apps/ skeleton

**Files:**
- Create: `/home/lkz/aleph-cxx/render/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/apps/CMakeLists.txt`
- Modify: `/home/lkz/aleph-cxx/CMakeLists.txt` — add SDL2 detection, add render/ and apps/ subdirs

- [ ] **Step 1.1: Verify branch**

Run: `cd /home/lkz/aleph-cxx && git branch --show-current`
Expected: `phase-2-render` (already created when spec was committed).
If branch is something else, fix: `git checkout phase-2-render`.

- [ ] **Step 1.2: Extend top-level `CMakeLists.txt`**

Append after the existing `add_subdirectory(foundation)` line and before `enable_testing()`:

```cmake
# ─── SDL2 detection ─────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
    endif()
endif()
if(SDL2_FOUND OR SDL2_LIBRARIES)
    set(ALEPH_HAVE_SDL2 ON)
    message(STATUS "aleph-cxx: SDL2 found — window + editor + aleph_sw will be built.")
else()
    set(ALEPH_HAVE_SDL2 OFF)
    message(STATUS "aleph-cxx: SDL2 NOT found — window + editor + aleph_sw skipped.")
endif()

add_subdirectory(render)
add_subdirectory(apps)
```

(Keep `add_subdirectory(bench)` and `enable_testing()` after these — DON'T move them.)

- [ ] **Step 1.3: Write `render/CMakeLists.txt`**

```cmake
# Module libraries added in subsequent tasks via add_subdirectory.
add_subdirectory(src/aleph.scene)
# add_subdirectory(src/aleph.render.common) — added in Task 8
# add_subdirectory(src/aleph.render.rt)     — added in Task 11
# add_subdirectory(src/aleph.render.sw)     — added in Task 14
# add_subdirectory(src/aleph.window)        — added in Task 19 (conditional)
# add_subdirectory(src/aleph.editor)        — added in Task 20 (conditional)
```

Create directory `render/src/aleph.scene/` and a placeholder file
`render/src/aleph.scene/CMakeLists.txt` with content `# placeholder — populated in Task 2`.

- [ ] **Step 1.4: Write `apps/CMakeLists.txt`**

```cmake
# Executables added in later tasks.
# add_subdirectory(aleph_rt)  — added in Task 24
# if(ALEPH_HAVE_SDL2)
#     add_subdirectory(aleph_sw)  — added in Task 25
# endif()
```

- [ ] **Step 1.5: Configure + build to verify no regression**

```bash
cd /home/lkz/aleph-cxx
cmake --preset release
cmake --build build-release 2>&1 | tail -5
ctest --test-dir build-release --output-on-failure 2>&1 | tail -10
```

Expected: 6/6 tests pass (no new tests yet — just confirming Phase 1 still green after CMake changes).

- [ ] **Step 1.6: Commit**

```bash
cd /home/lkz/aleph-cxx
git add CMakeLists.txt render/ apps/
git commit -m "task 1: render/ + apps/ skeleton + SDL2 detection"
```

---

## Task 2: `aleph.scene:handle32` + module skeleton

**Files:**
- Create: `/home/lkz/aleph-cxx/render/src/aleph.scene/CMakeLists.txt`
- Create: `/home/lkz/aleph-cxx/render/src/aleph.scene/aleph.scene.cppm`
- Create: `/home/lkz/aleph-cxx/render/src/aleph.scene/aleph.scene-handle32.cppm`
- Create: `/home/lkz/aleph-cxx/tests/scene/test_handle32.cpp`
- Modify: `/home/lkz/aleph-cxx/tests/CMakeLists.txt`

- [ ] **Step 2.1: Write failing test `tests/scene/test_handle32.cpp`**

```cpp
#include "doctest.h"
import aleph.scene;

using namespace aleph::scene;

TEST_CASE("Handle32 layout: 4 bytes packed") {
    static_assert(sizeof(Handle32) == 4);
}

TEST_CASE("Handle32 pack/unpack") {
    constexpr Handle32 h = Handle32::make(HittableKind::Sphere, 42);
    CHECK(h.hittable_kind() == HittableKind::Sphere);
    CHECK(h.index() == 42u);
}

TEST_CASE("Handle32 pack/unpack: BvhNode + max index") {
    constexpr Handle32 h = Handle32::make(HittableKind::BvhNode, 0x00FFFFFFu);
    CHECK(h.hittable_kind() == HittableKind::BvhNode);
    CHECK(h.index() == 0x00FFFFFFu);
}

TEST_CASE("Handle32: distinct packed values for different kinds at same idx") {
    constexpr Handle32 a = Handle32::make(HittableKind::Sphere, 7);
    constexpr Handle32 b = Handle32::make(HittableKind::Quad,   7);
    CHECK(a.packed != b.packed);
    CHECK(a.index() == b.index());
    CHECK(a.hittable_kind() != b.hittable_kind());
}

TEST_CASE("MaterialKind enumerators") {
    static_assert(static_cast<int>(MaterialKind::Lambertian) == 0);
    static_assert(static_cast<int>(MaterialKind::TexturedLambertian) == 4);
    CHECK(true);
}
```

`mkdir -p tests/scene` if needed.

- [ ] **Step 2.2: Update `tests/CMakeLists.txt`**

Append `scene/test_handle32.cpp` to the source list and `aleph_scene` to the link libs:

```cmake
add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp
    cpu/test_cpu.cpp
    math/test_types.cpp
    math/test_vec.cpp
    math/test_mat.cpp
    math/test_quat.cpp
    math/test_rotor.cpp
    math/test_bivector.cpp
    math/test_multivector.cpp
    math/test_rotor_advanced.cpp
    math/test_dual.cpp
    math/test_tangent.cpp
    math/test_geom.cpp
    alloc/test_arena.cpp
    alloc/test_frame.cpp
    alloc/test_slab.cpp
    alloc/test_freelist.cpp
    alloc/test_pmr_adapter.cpp
    containers/test_small_vector.cpp
    containers/test_flat_set.cpp
    containers/test_dense_index.cpp
    threads/test_pool.cpp
    threads/test_mpmc.cpp
    threads/test_work_stealing.cpp
    io/test_mmap.cpp
    io/test_ppm.cpp
    io/test_obj.cpp
    scene/test_handle32.cpp)
target_include_directories(aleph_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test
    aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads aleph_io
    aleph_scene)
add_test(NAME aleph_tests COMMAND aleph_tests)
```

- [ ] **Step 2.3: Run to verify it fails**

```bash
cd /home/lkz/aleph-cxx && cmake --build build-release --target aleph_tests 2>&1 | tail -5
```

Expected: error about `aleph.scene` / `aleph_scene` not found.

- [ ] **Step 2.4: Write `render/src/aleph.scene/aleph.scene-handle32.cppm`**

```cpp
module;
#include <cstdint>

export module aleph.scene:handle32;

export namespace aleph::scene {

enum class HittableKind : std::uint8_t {
    Sphere = 0,
    Quad   = 1,
    Tri    = 2,
    BvhNode = 3,
};

enum class MaterialKind : std::uint8_t {
    Lambertian         = 0,
    Metal              = 1,
    Dielectric         = 2,
    Emissive           = 3,
    TexturedLambertian = 4,
};

// Packed 32-bit handle: 8-bit kind tag + 24-bit dense index.
// Max 16,777,216 entities per kind.
struct Handle32 {
    std::uint32_t packed;

    constexpr HittableKind hittable_kind() const noexcept {
        return static_cast<HittableKind>(packed >> 24);
    }
    constexpr std::uint32_t index() const noexcept {
        return packed & 0x00FFFFFFu;
    }
    static constexpr Handle32 make(HittableKind k, std::uint32_t idx) noexcept {
        return Handle32{ (static_cast<std::uint32_t>(k) << 24) | (idx & 0x00FFFFFFu) };
    }
};

// 8-byte material handle. Not packed: not in the ray-hot-path; clarity wins.
struct MaterialHandle {
    MaterialKind kind;
    std::uint32_t idx;
};

}  // namespace aleph::scene
```

- [ ] **Step 2.5: Write primary `render/src/aleph.scene/aleph.scene.cppm`**

```cpp
export module aleph.scene;
export import :handle32;
```

- [ ] **Step 2.6: Write `render/src/aleph.scene/CMakeLists.txt`**

```cmake
add_library(aleph_scene)
target_sources(aleph_scene
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.scene.cppm
        aleph.scene-handle32.cppm)
target_link_libraries(aleph_scene
    PUBLIC  aleph_math aleph_containers aleph_alloc
    PRIVATE aleph_flags_isa)
```

- [ ] **Step 2.7: Build + run tests**

```bash
cd /home/lkz/aleph-cxx
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -5
```

Expected: all tests pass.

- [ ] **Step 2.8: Commit**

```bash
cd /home/lkz/aleph-cxx
git add render/src/aleph.scene/ tests/scene/test_handle32.cpp tests/CMakeLists.txt
git commit -m "task 2: aleph.scene:handle32 + HittableKind/MaterialKind enums"
```

---

## Task 3: `aleph.scene:sphere_soa`

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-sphere_soa.cppm`
- Create: `tests/scene/test_sphere_soa.cpp`
- Modify: `aleph.scene.cppm`, `aleph.scene/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 3.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("SphereSoA: append + index lookup") {
    SphereSoA s;
    const auto i0 = sphere_append(s, Vec3{1, 2, 3}, 0.5f,
                                   MaterialHandle{MaterialKind::Lambertian, 0});
    const auto i1 = sphere_append(s, Vec3{4, 5, 6}, 1.5f,
                                   MaterialHandle{MaterialKind::Metal,      0});
    CHECK(i0 == 0u);
    CHECK(i1 == 1u);
    CHECK(s.cx.size() == 2);
    CHECK(s.cx[0] == 1.0f); CHECK(s.cy[0] == 2.0f); CHECK(s.cz[0] == 3.0f);
    CHECK(s.r[0]  == 0.5f);
    CHECK(s.cx[1] == 4.0f); CHECK(s.cz[1] == 6.0f); CHECK(s.r[1] == 1.5f);
    CHECK(s.mat[0].kind == MaterialKind::Lambertian);
    CHECK(s.mat[1].kind == MaterialKind::Metal);
}

TEST_CASE("SphereSoA: bbox cached per sphere") {
    SphereSoA s;
    sphere_append(s, Vec3{0, 0, 0}, 1.0f, MaterialHandle{MaterialKind::Lambertian, 0});
    REQUIRE(s.bbox.size() == 1);
    CHECK(s.bbox[0].min == Vec3{-1, -1, -1});
    CHECK(s.bbox[0].max == Vec3{ 1,  1,  1});
}
```

- [ ] **Step 3.2: Update tests/CMakeLists.txt — append `scene/test_sphere_soa.cpp` to source list. Run to fail.**

- [ ] **Step 3.3: Write the module**

```cpp
module;
#include <cstdint>
#include <vector>

export module aleph.scene:sphere_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct SphereSoA {
    std::vector<aleph::math::f32> cx, cy, cz;
    std::vector<aleph::math::f32> r;
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
};

// Append a sphere; returns its u32 dense index (used to make a Handle32).
inline std::uint32_t sphere_append(SphereSoA& s,
                                    aleph::math::Vec3 center,
                                    aleph::math::f32  radius,
                                    MaterialHandle    m) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.cx.size());
    s.cx.push_back(center.x);
    s.cy.push_back(center.y);
    s.cz.push_back(center.z);
    s.r.push_back(radius);
    s.mat.push_back(m);
    s.bbox.push_back(aleph::math::Aabb{
        aleph::math::Vec3{center.x - radius, center.y - radius, center.z - radius},
        aleph::math::Vec3{center.x + radius, center.y + radius, center.z + radius},
    });
    return idx;
}

}  // namespace aleph::scene
```

- [ ] **Step 3.4: Wire up**

`aleph.scene.cppm`:
```cpp
export module aleph.scene;
export import :handle32;
export import :sphere_soa;
```

`render/src/aleph.scene/CMakeLists.txt` FILES list grows by `aleph.scene-sphere_soa.cppm`.

- [ ] **Step 3.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-sphere_soa.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_sphere_soa.cpp tests/CMakeLists.txt
git commit -m "task 3: aleph.scene:sphere_soa — SoA store + sphere_append (auto-bbox)"
```

---

## Task 4: `aleph.scene:quad_soa` + `tri_soa`

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-quad_soa.cppm`
- Create: `render/src/aleph.scene/aleph.scene-tri_soa.cppm`
- Create: `tests/scene/test_quad_tri_soa.cpp`

- [ ] **Step 4.1: Write failing test**

```cpp
#include "doctest.h"
#include <cmath>
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("QuadSoA: append computes normal + D + w + bbox") {
    QuadSoA q;
    // axis-aligned quad in xz plane at y=0: Q=(0,0,0), u=(1,0,0), v=(0,0,1)
    const auto i = quad_append(q, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1},
                                MaterialHandle{MaterialKind::Emissive, 0});
    CHECK(i == 0u);
    REQUIRE(q.Qx.size() == 1);
    CHECK(q.Qx[0] == 0.0f);  CHECK(q.Qy[0] == 0.0f);  CHECK(q.Qz[0] == 0.0f);
    CHECK(q.ux[0] == 1.0f);  CHECK(q.uy[0] == 0.0f);  CHECK(q.uz[0] == 0.0f);
    CHECK(q.vx[0] == 0.0f);  CHECK(q.vy[0] == 0.0f);  CHECK(q.vz[0] == 1.0f);
    // u × v = (1,0,0) × (0,0,1) = (0*1 - 0*0, 0*0 - 1*1, 1*0 - 0*0) = (0, -1, 0)
    // unit normal:
    CHECK(q.nx[0] == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(q.ny[0] == doctest::Approx(-1.0f).epsilon(1e-6));
    CHECK(q.nz[0] == doctest::Approx(0.0f).epsilon(1e-6));
    // D = n · Q = -1·0 = 0
    CHECK(q.D[0] == doctest::Approx(0.0f));
}

TEST_CASE("TriSoA: append stores three verts") {
    TriSoA t;
    const auto i = tri_append(t, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                               MaterialHandle{MaterialKind::Lambertian, 0});
    CHECK(i == 0u);
    CHECK(t.v0x[0] == 0.0f); CHECK(t.v1x[0] == 1.0f); CHECK(t.v2y[0] == 1.0f);
    CHECK(t.mat[0].kind == MaterialKind::Lambertian);
}
```

Run to fail.

- [ ] **Step 4.2: Write `aleph.scene-quad_soa.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>
#include <cmath>
#include <array>

export module aleph.scene:quad_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct QuadSoA {
    std::vector<aleph::math::f32> Qx, Qy, Qz;       // origin
    std::vector<aleph::math::f32> ux, uy, uz;       // edge u
    std::vector<aleph::math::f32> vx, vy, vz;       // edge v
    std::vector<aleph::math::f32> nx, ny, nz;       // unit normal
    std::vector<aleph::math::f32> D;                // n·p = D
    std::vector<aleph::math::Vec3> w;               // (u×v)/|u×v|² (cold)
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
};

inline std::uint32_t quad_append(QuadSoA& q,
                                  aleph::math::Vec3 Q,
                                  aleph::math::Vec3 u_edge,
                                  aleph::math::Vec3 v_edge,
                                  MaterialHandle    m) noexcept {
    const aleph::math::Vec3 n     = aleph::math::cross(u_edge, v_edge);
    const aleph::math::f32  n_lsq = aleph::math::length_sq(n);
    const aleph::math::f32  inv_n = 1.0f / std::sqrt(n_lsq);
    const aleph::math::Vec3 norm  = n * inv_n;
    const aleph::math::f32  D     = aleph::math::dot(norm, Q);
    const aleph::math::Vec3 w     = n * (1.0f / n_lsq);

    const std::array<aleph::math::Vec3, 4> corners{
        Q, Q + u_edge, Q + v_edge, Q + u_edge + v_edge
    };
    aleph::math::Aabb box = aleph::math::Aabb::from_points(corners);
    constexpr aleph::math::f32 eps = 1e-4f;
    if (box.max.x - box.min.x < eps) { box.min.x -= eps; box.max.x += eps; }
    if (box.max.y - box.min.y < eps) { box.min.y -= eps; box.max.y += eps; }
    if (box.max.z - box.min.z < eps) { box.min.z -= eps; box.max.z += eps; }

    const std::uint32_t idx = static_cast<std::uint32_t>(q.Qx.size());
    q.Qx.push_back(Q.x); q.Qy.push_back(Q.y); q.Qz.push_back(Q.z);
    q.ux.push_back(u_edge.x); q.uy.push_back(u_edge.y); q.uz.push_back(u_edge.z);
    q.vx.push_back(v_edge.x); q.vy.push_back(v_edge.y); q.vz.push_back(v_edge.z);
    q.nx.push_back(norm.x); q.ny.push_back(norm.y); q.nz.push_back(norm.z);
    q.D.push_back(D);
    q.w.push_back(w);
    q.mat.push_back(m);
    q.bbox.push_back(box);
    return idx;
}

}  // namespace aleph::scene
```

- [ ] **Step 4.3: Write `aleph.scene-tri_soa.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>
#include <array>

export module aleph.scene:tri_soa;

import aleph.math;
import :handle32;

export namespace aleph::scene {

struct TriSoA {
    std::vector<aleph::math::f32> v0x, v0y, v0z;
    std::vector<aleph::math::f32> v1x, v1y, v1z;
    std::vector<aleph::math::f32> v2x, v2y, v2z;
    std::vector<MaterialHandle>   mat;
    std::vector<aleph::math::Aabb> bbox;
};

inline std::uint32_t tri_append(TriSoA& t,
                                 aleph::math::Vec3 v0,
                                 aleph::math::Vec3 v1,
                                 aleph::math::Vec3 v2,
                                 MaterialHandle    m) noexcept {
    const std::array<aleph::math::Vec3, 3> corners{ v0, v1, v2 };
    aleph::math::Aabb box = aleph::math::Aabb::from_points(corners);
    constexpr aleph::math::f32 eps = 1e-4f;
    if (box.max.x - box.min.x < eps) { box.min.x -= eps; box.max.x += eps; }
    if (box.max.y - box.min.y < eps) { box.min.y -= eps; box.max.y += eps; }
    if (box.max.z - box.min.z < eps) { box.min.z -= eps; box.max.z += eps; }

    const std::uint32_t idx = static_cast<std::uint32_t>(t.v0x.size());
    t.v0x.push_back(v0.x); t.v0y.push_back(v0.y); t.v0z.push_back(v0.z);
    t.v1x.push_back(v1.x); t.v1y.push_back(v1.y); t.v1z.push_back(v1.z);
    t.v2x.push_back(v2.x); t.v2y.push_back(v2.y); t.v2z.push_back(v2.z);
    t.mat.push_back(m);
    t.bbox.push_back(box);
    return idx;
}

}  // namespace aleph::scene
```

- [ ] **Step 4.4: Wire + build + test + commit**

`aleph.scene.cppm` adds `export import :quad_soa; export import :tri_soa;`.
CMake FILE_SET grows by both files.
Tests CMakeLists adds `scene/test_quad_tri_soa.cpp`.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-quad_soa.cppm render/src/aleph.scene/aleph.scene-tri_soa.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_quad_tri_soa.cpp tests/CMakeLists.txt
git commit -m "task 4: aleph.scene:quad_soa + :tri_soa — quad with precomputed normal/D/w, tri with bbox"
```

---

## Task 5: `aleph.scene:material_soa`

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-material_soa.cppm`
- Create: `tests/scene/test_material_soa.cpp`

- [ ] **Step 5.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;
using aleph::math::Vec2;

TEST_CASE("LambertianSoA append + lookup") {
    LambertianSoA l;
    const auto i = lambertian_append(l, Vec3{0.5f, 0.3f, 0.1f});
    CHECK(i == 0u);
    CHECK(l.albedo[0] == Vec3{0.5f, 0.3f, 0.1f});
}

TEST_CASE("MetalSoA append: albedo + fuzz") {
    MetalSoA m;
    const auto i = metal_append(m, Vec3{0.7f, 0.7f, 0.7f}, 0.2f);
    CHECK(i == 0u);
    CHECK(m.albedo[0] == Vec3{0.7f, 0.7f, 0.7f});
    CHECK(m.fuzz[0] == 0.2f);
}

TEST_CASE("DielectricSoA: just ior") {
    DielectricSoA d;
    const auto i = dielectric_append(d, 1.5f);
    CHECK(i == 0u);
    CHECK(d.ior[0] == 1.5f);
}

TEST_CASE("EmissiveSoA: emit color") {
    EmissiveSoA e;
    const auto i = emissive_append(e, Vec3{15, 15, 15});
    CHECK(i == 0u);
    CHECK(e.emit[0] == Vec3{15, 15, 15});
}

TEST_CASE("TexturedLambertianSoA: tex_id + uv_scale") {
    TexturedLambertianSoA t;
    const auto i = textured_lambertian_append(t, 7u, Vec2{2.0f, 1.0f});
    CHECK(i == 0u);
    CHECK(t.tex_id[0] == 7u);
    CHECK(t.uv_scale[0] == Vec2{2.0f, 1.0f});
}
```

- [ ] **Step 5.2: Write the module**

```cpp
module;
#include <cstdint>
#include <vector>

export module aleph.scene:material_soa;

import aleph.math;

export namespace aleph::scene {

struct LambertianSoA {
    std::vector<aleph::math::Vec3> albedo;
};
inline std::uint32_t lambertian_append(LambertianSoA& s, aleph::math::Vec3 albedo) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.albedo.size());
    s.albedo.push_back(albedo);
    return idx;
}

struct MetalSoA {
    std::vector<aleph::math::Vec3> albedo;
    std::vector<aleph::math::f32>  fuzz;
};
inline std::uint32_t metal_append(MetalSoA& s, aleph::math::Vec3 albedo, aleph::math::f32 fuzz) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.albedo.size());
    s.albedo.push_back(albedo);
    s.fuzz.push_back(fuzz);
    return idx;
}

struct DielectricSoA {
    std::vector<aleph::math::f32> ior;
};
inline std::uint32_t dielectric_append(DielectricSoA& s, aleph::math::f32 ior) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.ior.size());
    s.ior.push_back(ior);
    return idx;
}

struct EmissiveSoA {
    std::vector<aleph::math::Vec3> emit;
};
inline std::uint32_t emissive_append(EmissiveSoA& s, aleph::math::Vec3 emit) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.emit.size());
    s.emit.push_back(emit);
    return idx;
}

struct TexturedLambertianSoA {
    std::vector<std::uint32_t>     tex_id;
    std::vector<aleph::math::Vec2> uv_scale;
};
inline std::uint32_t textured_lambertian_append(TexturedLambertianSoA& s,
                                                  std::uint32_t tex_id,
                                                  aleph::math::Vec2 uv_scale) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.tex_id.size());
    s.tex_id.push_back(tex_id);
    s.uv_scale.push_back(uv_scale);
    return idx;
}

}  // namespace aleph::scene
```

- [ ] **Step 5.3: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-material_soa.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_material_soa.cpp tests/CMakeLists.txt
git commit -m "task 5: aleph.scene:material_soa — 5 material SoA stores + appenders"
```

---

## Task 6: `aleph.scene:scene` — top-level Scene + add helpers

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-scene.cppm`
- Create: `tests/scene/test_scene.cpp`

- [ ] **Step 6.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("Scene starts empty") {
    Scene s;
    CHECK(s.spheres.cx.empty());
    CHECK(s.lights.empty());
    CHECK(s.textures.empty());
}

TEST_CASE("scene_add_sphere returns Handle32 with correct kind+idx") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    const auto h = scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    CHECK(h.hittable_kind() == HittableKind::Sphere);
    CHECK(h.index() == 0u);
    CHECK(s.spheres.cx.size() == 1);
}

TEST_CASE("scene_add_quad with emissive material auto-registers in lights list") {
    Scene s;
    const auto m = scene_add_emissive(s, Vec3{15, 15, 15});
    const auto h = scene_add_quad(s, Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}, m);
    CHECK(h.hittable_kind() == HittableKind::Quad);
    REQUIRE(s.lights.size() == 1);
    CHECK(s.lights[0].packed == h.packed);
}

TEST_CASE("Non-emissive quad does NOT add to lights") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_quad(s, Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}, m);
    CHECK(s.lights.empty());
}

TEST_CASE("scene_add_metal returns material handle with correct kind") {
    Scene s;
    const auto m = scene_add_metal(s, Vec3{0.7f, 0.6f, 0.5f}, 0.1f);
    CHECK(m.kind == MaterialKind::Metal);
    CHECK(m.idx == 0u);
}
```

- [ ] **Step 6.2: Write `aleph.scene-scene.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>

export module aleph.scene:scene;

import aleph.math;
import aleph.common;     // for Image (texture pool entries)
import :handle32;
import :sphere_soa;
import :quad_soa;
import :tri_soa;
import :material_soa;

export namespace aleph::scene {

struct BvhNode;          // forward — defined in :bvh
struct BvhNodeArr {
    std::vector<BvhNode> nodes;
};

struct Scene {
    // Hittables
    SphereSoA spheres;
    QuadSoA   quads;
    TriSoA    tris;
    // Materials
    LambertianSoA           lamb;
    MetalSoA                metal;
    DielectricSoA           diel;
    EmissiveSoA             emis;
    TexturedLambertianSoA   tex_lamb;
    // Textures (referenced by tex_lamb)
    std::vector<aleph::common::Image> textures;
    // NEE light list (emissive quads/spheres/tris)
    std::vector<Handle32> lights;
    // Spatial structure (built after population)
    BvhNodeArr bvh;
};

// ─── Material adders ────────────────────────────────────────────────
inline MaterialHandle scene_add_lambertian(Scene& s, aleph::math::Vec3 albedo) {
    return MaterialHandle{MaterialKind::Lambertian, lambertian_append(s.lamb, albedo)};
}
inline MaterialHandle scene_add_metal(Scene& s, aleph::math::Vec3 albedo, aleph::math::f32 fuzz) {
    return MaterialHandle{MaterialKind::Metal, metal_append(s.metal, albedo, fuzz)};
}
inline MaterialHandle scene_add_dielectric(Scene& s, aleph::math::f32 ior) {
    return MaterialHandle{MaterialKind::Dielectric, dielectric_append(s.diel, ior)};
}
inline MaterialHandle scene_add_emissive(Scene& s, aleph::math::Vec3 emit) {
    return MaterialHandle{MaterialKind::Emissive, emissive_append(s.emis, emit)};
}
inline MaterialHandle scene_add_textured_lambertian(Scene& s, std::uint32_t tex_id,
                                                      aleph::math::Vec2 uv_scale) {
    return MaterialHandle{MaterialKind::TexturedLambertian,
                          textured_lambertian_append(s.tex_lamb, tex_id, uv_scale)};
}

// ─── Hittable adders (auto-register emissive ones in lights) ────────
inline Handle32 scene_add_sphere(Scene& s, aleph::math::Vec3 center, aleph::math::f32 r,
                                  MaterialHandle m) {
    const std::uint32_t idx = sphere_append(s.spheres, center, r, m);
    const Handle32 h = Handle32::make(HittableKind::Sphere, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

inline Handle32 scene_add_quad(Scene& s, aleph::math::Vec3 Q, aleph::math::Vec3 u_edge,
                                aleph::math::Vec3 v_edge, MaterialHandle m) {
    const std::uint32_t idx = quad_append(s.quads, Q, u_edge, v_edge, m);
    const Handle32 h = Handle32::make(HittableKind::Quad, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

inline Handle32 scene_add_tri(Scene& s, aleph::math::Vec3 v0, aleph::math::Vec3 v1,
                               aleph::math::Vec3 v2, MaterialHandle m) {
    const std::uint32_t idx = tri_append(s.tris, v0, v1, v2, m);
    const Handle32 h = Handle32::make(HittableKind::Tri, idx);
    if (m.kind == MaterialKind::Emissive) s.lights.push_back(h);
    return h;
}

}  // namespace aleph::scene
```

- [ ] **Step 6.3: Wire + build + test + commit**

Update primary unit (`export import :scene;`), CMake FILE_SET, tests CMakeLists.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-scene.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_scene.cpp tests/CMakeLists.txt
git commit -m "task 6: aleph.scene:scene — Scene struct + scene_add_* helpers + auto light registration"
```

---

## Task 7: `aleph.scene:bvh` — node + build

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-bvh.cppm`
- Create: `tests/scene/test_bvh_build.cpp`

- [ ] **Step 7.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("scene_build_bvh: 1 sphere → single leaf root") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    alignas(16) static unsigned char scratch[16384];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    // 1 input → 1 node (the leaf itself).
    REQUIRE(s.bvh.nodes.size() == 1);
    const BvhNode& root = s.bvh.nodes[0];
    CHECK(root.left.hittable_kind() == HittableKind::Sphere);
    CHECK(root.left.index() == 0u);
    CHECK(root.right.packed == root.left.packed);   // single-leaf: right=left sentinel
}

TEST_CASE("scene_build_bvh: 3 mixed primitives → internal node + leaves") {
    Scene s;
    const auto m_lamb = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    const auto m_met  = scene_add_metal(s, Vec3{0.7f, 0.7f, 0.7f}, 0.0f);
    scene_add_sphere(s, Vec3{-5, 0, 0}, 0.5f, m_lamb);
    scene_add_sphere(s, Vec3{ 0, 0, 0}, 0.5f, m_met);
    scene_add_sphere(s, Vec3{ 5, 0, 0}, 0.5f, m_lamb);
    alignas(16) static unsigned char scratch[16384];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    REQUIRE(s.bvh.nodes.size() >= 3);   // 3 leaves + at least 1 internal
    // Root is the LAST node (the build appends in post-order).
    const BvhNode& root = s.bvh.nodes.back();
    CHECK(root.left.hittable_kind() == HittableKind::BvhNode);  // both children are BvhNodes (internal)
    // Bbox encloses all spheres
    CHECK(root.bbox.min.x <= -5.5f);
    CHECK(root.bbox.max.x >=  5.5f);
}
```

- [ ] **Step 7.2: Write `aleph.scene-bvh.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>
#include <span>
#include <algorithm>

export module aleph.scene:bvh;

import aleph.math;
import aleph.alloc;
import :handle32;
import :scene;
import :sphere_soa;
import :quad_soa;
import :tri_soa;

export namespace aleph::scene {

struct BvhNode {
    aleph::math::Aabb bbox;
    Handle32          left;
    Handle32          right;
};

namespace detail {

// Lookup the bbox for any primitive Handle32.
inline aleph::math::Aabb primitive_bbox(const Scene& s, Handle32 h) noexcept {
    switch (h.hittable_kind()) {
        case HittableKind::Sphere:  return s.spheres.bbox[h.index()];
        case HittableKind::Quad:    return s.quads.bbox[h.index()];
        case HittableKind::Tri:     return s.tris.bbox[h.index()];
        case HittableKind::BvhNode: return s.bvh.nodes[h.index()].bbox;
    }
    return aleph::math::Aabb{};   // unreachable
}

inline aleph::math::f32 centroid_axis(const aleph::math::Aabb& b, int axis) noexcept {
    switch (axis) {
        case 0:  return b.min.x + b.max.x;
        case 1:  return b.min.y + b.max.y;
        default: return b.min.z + b.max.z;
    }
}

inline int longest_axis(const aleph::math::Aabb& b) noexcept {
    const aleph::math::f32 dx = b.max.x - b.min.x;
    const aleph::math::f32 dy = b.max.y - b.min.y;
    const aleph::math::f32 dz = b.max.z - b.min.z;
    if (dx >= dy && dx >= dz) return 0;
    if (dy >= dz)              return 1;
    return 2;
}

// Recursive build; returns the Handle32 of the produced node (leaf or BvhNode).
// `items` is a temp span we sort in place; backing storage lives in arena.
inline Handle32 build_recursive(Scene& s, std::span<Handle32> items) {
    if (items.size() == 1) return items[0];

    aleph::math::Aabb combined = primitive_bbox(s, items[0]);
    for (std::size_t i = 1; i < items.size(); ++i)
        combined = aleph::math::union_of(combined, primitive_bbox(s, items[i]));

    const int axis = longest_axis(combined);
    std::ranges::sort(items, [&s, axis](Handle32 a, Handle32 b) noexcept {
        return centroid_axis(primitive_bbox(s, a), axis)
             < centroid_axis(primitive_bbox(s, b), axis);
    });

    const std::size_t mid = items.size() / 2;
    const Handle32 left  = build_recursive(s, items.first(mid));
    const Handle32 right = build_recursive(s, items.subspan(mid));

    const std::uint32_t node_idx = static_cast<std::uint32_t>(s.bvh.nodes.size());
    s.bvh.nodes.push_back(BvhNode{
        aleph::math::union_of(primitive_bbox(s, left), primitive_bbox(s, right)),
        left, right,
    });
    return Handle32::make(HittableKind::BvhNode, node_idx);
}

}  // namespace detail

// Collect all primitive Handle32s of the scene, then call build_recursive.
// The root ends up as the LAST node in s.bvh.nodes (the recursion appends post-order).
inline void scene_build_bvh(Scene& s, aleph::alloc::Arena& scratch) {
    (void)scratch;   // future: use arena for the items vector; v1 uses std::vector
    s.bvh.nodes.clear();

    std::vector<Handle32> items;
    items.reserve(s.spheres.cx.size() + s.quads.Qx.size() + s.tris.v0x.size());
    for (std::uint32_t i = 0; i < s.spheres.cx.size(); ++i)
        items.push_back(Handle32::make(HittableKind::Sphere, i));
    for (std::uint32_t i = 0; i < s.quads.Qx.size(); ++i)
        items.push_back(Handle32::make(HittableKind::Quad, i));
    for (std::uint32_t i = 0; i < s.tris.v0x.size(); ++i)
        items.push_back(Handle32::make(HittableKind::Tri, i));

    if (items.empty()) return;

    if (items.size() == 1) {
        // Single primitive: synthesize a degenerate "leaf-node" so callers
        // can still walk s.bvh.nodes uniformly. left=right=the primitive.
        s.bvh.nodes.push_back(BvhNode{
            detail::primitive_bbox(s, items[0]),
            items[0], items[0],
        });
        return;
    }

    detail::build_recursive(s, std::span<Handle32>{items});
}

}  // namespace aleph::scene
```

- [ ] **Step 7.3: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-bvh.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_bvh_build.cpp tests/CMakeLists.txt
git commit -m "task 7: aleph.scene:bvh — BvhNode + scene_build_bvh (median-split, post-order append)"
```

---

## Task 8: `aleph.scene:hit` — closest-hit dispatch

**Files:**
- Create: `render/src/aleph.scene/aleph.scene-hit.cppm`
- Create: `tests/scene/test_hit.cpp`

- [ ] **Step 8.1: Write failing test**

```cpp
#include "doctest.h"
#include <limits>
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::scene;
using aleph::math::Vec3;
using aleph::math::Ray;

TEST_CASE("hit on empty scene returns nullopt") {
    Scene s;
    alignas(16) static unsigned char scratch[1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    auto r = hit(s, Ray{Vec3{0,0,-5}, Vec3{0,0,1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    CHECK_FALSE(r.has_value());
}

TEST_CASE("hit: single sphere — ray straight in hits it") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    alignas(16) static unsigned char scratch[2048];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);

    auto r = hit(s, Ray{Vec3{0, 0, -5}, Vec3{0, 0, 1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    REQUIRE(r.has_value());
    CHECK(r->t == doctest::Approx(4.0f));
    CHECK(r->front_face);
    CHECK(r->mat.kind == MaterialKind::Lambertian);
}

TEST_CASE("hit: 3 spheres in a row — closest one returned") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0,  5}, 0.5f, m);
    scene_add_sphere(s, Vec3{0, 0,  0}, 0.5f, m);
    scene_add_sphere(s, Vec3{0, 0, -5}, 0.5f, m);
    alignas(16) static unsigned char scratch[4096];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    // Ray from -10 going +Z: should hit z=-5 sphere first (t≈4.5).
    auto r = hit(s, Ray{Vec3{0, 0, -10}, Vec3{0, 0, 1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    REQUIRE(r.has_value());
    CHECK(r->t == doctest::Approx(4.5f).epsilon(1e-5f));
}
```

- [ ] **Step 8.2: Write `aleph.scene-hit.cppm`**

```cpp
module;
#include <cstdint>
#include <optional>
#include <cmath>
#include <array>

export module aleph.scene:hit;

import aleph.math;
import :handle32;
import :scene;
import :bvh;

export namespace aleph::scene {

struct HitRecord {
    aleph::math::Vec3 p;
    aleph::math::Vec3 normal;        // oriented against incident ray
    aleph::math::f32  t;
    aleph::math::f32  u, v;
    MaterialHandle    mat;
    bool              front_face;
};

namespace detail {

// Ray-vs-AABB slab test (same as Sotark cxx26 / Phase 1 reference).
inline bool aabb_hit(aleph::math::Aabb box, aleph::math::Ray r,
                      aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    {
        const aleph::math::f32 inv = 1.0f / r.dir.x;
        aleph::math::f32 t0 = (box.min.x - r.origin.x) * inv;
        aleph::math::f32 t1 = (box.max.x - r.origin.x) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    {
        const aleph::math::f32 inv = 1.0f / r.dir.y;
        aleph::math::f32 t0 = (box.min.y - r.origin.y) * inv;
        aleph::math::f32 t1 = (box.max.y - r.origin.y) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    {
        const aleph::math::f32 inv = 1.0f / r.dir.z;
        aleph::math::f32 t0 = (box.min.z - r.origin.z) * inv;
        aleph::math::f32 t1 = (box.max.z - r.origin.z) * inv;
        if (t0 > t1) { const auto t = t0; t0 = t1; t1 = t; }
        if (t0 > t_min) t_min = t0;
        if (t1 < t_max) t_max = t1;
        if (t_max <= t_min) return false;
    }
    return true;
}

inline std::optional<HitRecord>
hit_sphere(const SphereSoA& s, std::uint32_t idx, aleph::math::Ray r,
            aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 center{s.cx[idx], s.cy[idx], s.cz[idx]};
    const aleph::math::f32  radius = s.r[idx];
    const aleph::math::Vec3 oc = r.origin - center;
    const aleph::math::f32 a  = aleph::math::dot(r.dir, r.dir);
    const aleph::math::f32 hf = aleph::math::dot(oc, r.dir);
    const aleph::math::f32 c  = aleph::math::dot(oc, oc) - radius * radius;
    const aleph::math::f32 disc = hf * hf - a * c;
    if (disc < 0.0f) return std::nullopt;
    const aleph::math::f32 sd = std::sqrt(disc);
    aleph::math::f32 root = (-hf - sd) / a;
    if (root <= t_min || root >= t_max) {
        root = (-hf + sd) / a;
        if (root <= t_min || root >= t_max) return std::nullopt;
    }
    HitRecord rec{};
    rec.t   = root;
    rec.p   = r.at(root);
    rec.mat = s.mat[idx];
    const aleph::math::Vec3 outward = (rec.p - center) * (1.0f / radius);
    rec.front_face = aleph::math::dot(r.dir, outward) < 0.0f;
    rec.normal     = rec.front_face ? outward : -outward;
    // Spherical UV.
    const aleph::math::f32 theta = std::acos(-outward.y);
    const aleph::math::f32 phi   = std::atan2(-outward.z, outward.x) + aleph::math::pi_f;
    rec.u = phi   / aleph::math::two_pi_f;
    rec.v = theta / aleph::math::pi_f;
    return rec;
}

inline std::optional<HitRecord>
hit_quad(const QuadSoA& q, std::uint32_t idx, aleph::math::Ray r,
          aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 n{q.nx[idx], q.ny[idx], q.nz[idx]};
    const aleph::math::f32 denom = aleph::math::dot(n, r.dir);
    if (std::abs(denom) < 1e-8f) return std::nullopt;
    const aleph::math::f32 t = (q.D[idx] - aleph::math::dot(n, r.origin)) / denom;
    if (t <= t_min || t >= t_max) return std::nullopt;
    const aleph::math::Vec3 P = r.at(t);
    const aleph::math::Vec3 Q{q.Qx[idx], q.Qy[idx], q.Qz[idx]};
    const aleph::math::Vec3 u_edge{q.ux[idx], q.uy[idx], q.uz[idx]};
    const aleph::math::Vec3 v_edge{q.vx[idx], q.vy[idx], q.vz[idx]};
    const aleph::math::Vec3 hit_vec = P - Q;
    const aleph::math::f32 alpha = aleph::math::dot(q.w[idx], aleph::math::cross(hit_vec, v_edge));
    const aleph::math::f32 beta  = aleph::math::dot(q.w[idx], aleph::math::cross(u_edge, hit_vec));
    if (alpha < 0.0f || alpha > 1.0f || beta < 0.0f || beta > 1.0f) return std::nullopt;
    HitRecord rec{};
    rec.t   = t;
    rec.p   = P;
    rec.mat = q.mat[idx];
    rec.u   = alpha;
    rec.v   = beta;
    rec.front_face = aleph::math::dot(r.dir, n) < 0.0f;
    rec.normal     = rec.front_face ? n : -n;
    return rec;
}

inline std::optional<HitRecord>
hit_tri(const TriSoA& t, std::uint32_t idx, aleph::math::Ray r,
         aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 v0{t.v0x[idx], t.v0y[idx], t.v0z[idx]};
    const aleph::math::Vec3 v1{t.v1x[idx], t.v1y[idx], t.v1z[idx]};
    const aleph::math::Vec3 v2{t.v2x[idx], t.v2y[idx], t.v2z[idx]};
    const aleph::math::Vec3 e1 = v1 - v0;
    const aleph::math::Vec3 e2 = v2 - v0;
    const aleph::math::Vec3 pvec = aleph::math::cross(r.dir, e2);
    const aleph::math::f32 det  = aleph::math::dot(e1, pvec);
    if (std::abs(det) < 1e-8f) return std::nullopt;
    const aleph::math::f32 inv_det = 1.0f / det;
    const aleph::math::Vec3 tvec = r.origin - v0;
    const aleph::math::f32 u = inv_det * aleph::math::dot(tvec, pvec);
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const aleph::math::Vec3 qvec = aleph::math::cross(tvec, e1);
    const aleph::math::f32 v = inv_det * aleph::math::dot(r.dir, qvec);
    if (v < 0.0f || (u + v) > 1.0f) return std::nullopt;
    const aleph::math::f32 tt = inv_det * aleph::math::dot(e2, qvec);
    if (tt <= t_min || tt >= t_max) return std::nullopt;
    HitRecord rec{};
    rec.t   = tt;
    rec.p   = r.at(tt);
    rec.mat = t.mat[idx];
    const aleph::math::Vec3 outward = aleph::math::normalize(aleph::math::cross(e1, e2));
    rec.front_face = aleph::math::dot(r.dir, outward) < 0.0f;
    rec.normal     = rec.front_face ? outward : -outward;
    return rec;
}

}  // namespace detail

// Closest-hit query. Stack-based BVH traversal — no recursion.
[[nodiscard]] inline std::optional<HitRecord>
hit(const Scene& s, aleph::math::Ray r,
     aleph::math::f32 t_min, aleph::math::f32 t_max) noexcept {
    if (s.bvh.nodes.empty()) return std::nullopt;

    // Start from root (last node in the post-order array).
    const std::uint32_t root_idx = static_cast<std::uint32_t>(s.bvh.nodes.size() - 1);

    // Explicit stack — 64 entries is plenty for any reasonable scene.
    std::array<std::uint32_t, 64> stack{};
    int sp = 0;
    stack[sp++] = root_idx;

    std::optional<HitRecord> best;
    aleph::math::f32 closest = t_max;

    while (sp > 0) {
        const std::uint32_t ni = stack[--sp];
        const BvhNode& node = s.bvh.nodes[ni];
        if (!detail::aabb_hit(node.bbox, r, t_min, closest)) continue;

        // Helper: process a child handle (leaf primitive OR internal node).
        auto process = [&](Handle32 h) {
            switch (h.hittable_kind()) {
                case HittableKind::BvhNode:
                    if (sp < 64) stack[sp++] = h.index();
                    break;
                case HittableKind::Sphere:
                    if (auto rec = detail::hit_sphere(s.spheres, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
                case HittableKind::Quad:
                    if (auto rec = detail::hit_quad(s.quads, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
                case HittableKind::Tri:
                    if (auto rec = detail::hit_tri(s.tris, h.index(), r, t_min, closest); rec) {
                        closest = rec->t; best = rec;
                    }
                    break;
            }
        };
        process(node.left);
        // Single-primitive scene (Task 7): left == right; skip the second process to avoid double-hit.
        if (node.right.packed != node.left.packed) process(node.right);
    }
    return best;
}

}  // namespace aleph::scene
```

- [ ] **Step 8.3: Wire + build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.scene/aleph.scene-hit.cppm render/src/aleph.scene/aleph.scene.cppm render/src/aleph.scene/CMakeLists.txt tests/scene/test_hit.cpp tests/CMakeLists.txt
git commit -m "task 8: aleph.scene:hit — closest-hit query (stack-based BVH traversal); scene module complete"
```

---

## Task 9: `aleph.render.common:camera` + `:sky`

**Files:**
- Create: `render/src/aleph.render.common/CMakeLists.txt`
- Create: `render/src/aleph.render.common/aleph.render.common.cppm`
- Create: `render/src/aleph.render.common/aleph.render.common-camera.cppm`
- Create: `render/src/aleph.render.common/aleph.render.common-sky.cppm`
- Create: `tests/render/test_camera.cpp`
- Modify: `render/CMakeLists.txt` → uncomment `add_subdirectory(src/aleph.render.common)`
- Modify: `tests/CMakeLists.txt` (add `render/test_camera.cpp` + `aleph_render_common` to link)

- [ ] **Step 9.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.render.common;
import aleph.math;

using namespace aleph::render::common;
using aleph::math::Vec3;

TEST_CASE("make_camera: pinhole when defocus_angle <= 0") {
    Camera c = make_camera(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                            60.0f, 100, 100, 0.0f, 1.0f);
    CHECK_FALSE(c.has_defocus);
    CHECK(c.center == Vec3{0, 0, 5});
}

TEST_CASE("camera_get_ray: pixel (50,50) on a 100x100 image with center origin maps near forward") {
    Camera c = make_camera(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                            60.0f, 100, 100, 0.0f, 1.0f);
    aleph::math::Pcg32 rng(42, 54);
    aleph::math::Ray r = camera_get_ray(c, 50, 50, rng);
    CHECK(r.origin == Vec3{0, 0, 5});
    // dir points roughly -Z (since lookat is origin)
    CHECK(r.dir.z < 0.0f);
}

TEST_CASE("sky_sample: gradient interpolates between low and high") {
    Sky s{ Vec3{0, 0, 0}, Vec3{1, 1, 1} };
    // unit dir pointing +y → a = 0.5*(1+1) = 1.0 → all "high"
    Vec3 c = sky_sample(s, Vec3{0, 1, 0});
    CHECK(c == Vec3{1, 1, 1});
    // unit dir pointing -y → a = 0.5*(-1+1) = 0.0 → all "low"
    c = sky_sample(s, Vec3{0, -1, 0});
    CHECK(c == Vec3{0, 0, 0});
}
```

- [ ] **Step 9.2: Write `aleph.render.common-camera.cppm`**

```cpp
module;
#include <cmath>

export module aleph.render.common:camera;

import aleph.math;

export namespace aleph::render::common {

struct Camera {
    aleph::math::Vec3 center{};
    aleph::math::Vec3 pixel00_loc{};
    aleph::math::Vec3 pixel_delta_u{};
    aleph::math::Vec3 pixel_delta_v{};
    aleph::math::Vec3 defocus_disk_u{};
    aleph::math::Vec3 defocus_disk_v{};
    bool              has_defocus{false};
};

inline Camera make_camera(aleph::math::Vec3 lookfrom, aleph::math::Vec3 lookat,
                           aleph::math::Vec3 vup,
                           aleph::math::f32 vfov_deg,
                           int image_width, int image_height,
                           aleph::math::f32 defocus_angle_deg,
                           aleph::math::f32 focus_dist) noexcept {
    Camera c{};
    c.center = lookfrom;
    const aleph::math::f32 theta = aleph::math::deg_to_rad(vfov_deg);
    const aleph::math::f32 h     = std::tan(theta * 0.5f);
    const aleph::math::f32 vp_h  = 2.0f * h * focus_dist;
    const aleph::math::f32 vp_w  = vp_h *
        static_cast<aleph::math::f32>(image_width) / static_cast<aleph::math::f32>(image_height);

    const aleph::math::Vec3 w = aleph::math::normalize(lookfrom - lookat);
    const aleph::math::Vec3 u = aleph::math::normalize(aleph::math::cross(vup, w));
    const aleph::math::Vec3 v = aleph::math::cross(w, u);

    const aleph::math::Vec3 vp_u = u *  vp_w;
    const aleph::math::Vec3 vp_v = -v * vp_h;
    c.pixel_delta_u = vp_u * (1.0f / static_cast<aleph::math::f32>(image_width));
    c.pixel_delta_v = vp_v * (1.0f / static_cast<aleph::math::f32>(image_height));

    const aleph::math::Vec3 vp_upper_left =
        lookfrom - w * focus_dist - vp_u * 0.5f - vp_v * 0.5f;
    c.pixel00_loc = vp_upper_left + (c.pixel_delta_u + c.pixel_delta_v) * 0.5f;

    c.has_defocus = defocus_angle_deg > 0.0f;
    if (c.has_defocus) {
        const aleph::math::f32 defocus_rad =
            focus_dist * std::tan(aleph::math::deg_to_rad(defocus_angle_deg) * 0.5f);
        c.defocus_disk_u = u * defocus_rad;
        c.defocus_disk_v = v * defocus_rad;
    }
    return c;
}

inline aleph::math::Ray camera_get_ray(const Camera& c, int px, int py,
                                        aleph::math::Pcg32& rng) noexcept {
    const aleph::math::f32 du = rng.float01() - 0.5f;
    const aleph::math::f32 dv = rng.float01() - 0.5f;
    const aleph::math::Vec3 sample =
        c.pixel00_loc
        + c.pixel_delta_u * (static_cast<aleph::math::f32>(px) + du)
        + c.pixel_delta_v * (static_cast<aleph::math::f32>(py) + dv);

    aleph::math::Vec3 origin = c.center;
    if (c.has_defocus) {
        // Rejection sample in unit disk.
        aleph::math::Vec3 disk{};
        for (;;) {
            const aleph::math::f32 x = rng.float01() * 2.0f - 1.0f;
            const aleph::math::f32 y = rng.float01() * 2.0f - 1.0f;
            if (x*x + y*y < 1.0f) { disk = aleph::math::Vec3{x, y, 0.0f}; break; }
        }
        origin = c.center + c.defocus_disk_u * disk.x + c.defocus_disk_v * disk.y;
    }
    return aleph::math::Ray{origin, sample - origin};
}

}  // namespace aleph::render::common
```

- [ ] **Step 9.3: Write `aleph.render.common-sky.cppm`**

```cpp
export module aleph.render.common:sky;

import aleph.math;

export namespace aleph::render::common {

struct Sky {
    aleph::math::Vec3 low;
    aleph::math::Vec3 high;
};

// Gradient sample. `unit_dir` must be unit-length.
inline aleph::math::Vec3 sky_sample(const Sky& s, aleph::math::Vec3 unit_dir) noexcept {
    const aleph::math::f32 a = 0.5f * (unit_dir.y + 1.0f);
    return aleph::math::lerp(s.low, s.high, a);
}

}  // namespace aleph::render::common
```

- [ ] **Step 9.4: Primary + CMake + wire**

`aleph.render.common.cppm`:
```cpp
export module aleph.render.common;
export import :camera;
export import :sky;
```

`render/src/aleph.render.common/CMakeLists.txt`:
```cmake
add_library(aleph_render_common)
target_sources(aleph_render_common
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.common.cppm
        aleph.render.common-camera.cppm
        aleph.render.common-sky.cppm)
target_link_libraries(aleph_render_common
    PUBLIC  aleph_math
    PRIVATE aleph_flags_isa)
```

Uncomment in `render/CMakeLists.txt`. Add `render/test_camera.cpp` to tests CMakeLists and `aleph_render_common` to link libs.

- [ ] **Step 9.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.common/ render/CMakeLists.txt tests/render/test_camera.cpp tests/CMakeLists.txt
git commit -m "task 9: aleph.render.common:camera + :sky"
```

`mkdir -p tests/render` if needed before step 9.1.

---

## Task 10: `aleph.render.common:film` + `:tonemap`

**Files:**
- Create: `render/src/aleph.render.common/aleph.render.common-film.cppm`
- Create: `render/src/aleph.render.common/aleph.render.common-tonemap.cppm`
- Create: `tests/render/test_film.cpp`
- Create: `tests/render/test_tonemap.cpp`

- [ ] **Step 10.1: Write failing tests**

`test_film.cpp`:
```cpp
#include "doctest.h"
import aleph.render.common;
import aleph.alloc;
import aleph.math;

using namespace aleph::render::common;

TEST_CASE("film_alloc backs pixels with an Arena") {
    alignas(64) static unsigned char scratch[64 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    Film f = film_alloc(arena, 16, 16);
    CHECK(f.width == 16);
    CHECK(f.height == 16);
    CHECK(f.stride_pixels == 16);
    REQUIRE(f.pixels != nullptr);
    f.pixels[0] = aleph::math::Vec3{1, 0, 0};
    f.pixels[16] = aleph::math::Vec3{0, 1, 0};
    CHECK(f.pixels[0]  == aleph::math::Vec3{1, 0, 0});
    CHECK(f.pixels[16] == aleph::math::Vec3{0, 1, 0});
}
```

`test_tonemap.cpp`:
```cpp
#include "doctest.h"
import aleph.render.common;
import aleph.math;

using namespace aleph::render::common;

TEST_CASE("byte_from_linear: 0.0 → 0, 1.0 → 255") {
    CHECK(byte_from_linear(0.0f) == 0u);
    CHECK(byte_from_linear(1.0f) == 255u);
    CHECK(byte_from_linear(0.25f) > 0u);
    CHECK(byte_from_linear(0.25f) < 255u);
}

TEST_CASE("tonemap_argb8888_gamma2: pure red linear -> 0xFFFF0000") {
    const auto pixel = tonemap_argb8888_gamma2(aleph::math::Vec3{1.0f, 0.0f, 0.0f});
    CHECK(((pixel >> 24) & 0xFFu) == 0xFFu);     // alpha=255
    CHECK(((pixel >> 16) & 0xFFu) == 0xFFu);     // R=255
    CHECK(((pixel >>  8) & 0xFFu) == 0x00u);     // G=0
    CHECK((pixel        & 0xFFu) == 0x00u);     // B=0
}
```

- [ ] **Step 10.2: Write `aleph.render.common-film.cppm`**

```cpp
module;
#include <cstdint>

export module aleph.render.common:film;

import aleph.math;
import aleph.alloc;

export namespace aleph::render::common {

struct Film {
    aleph::math::Vec3* pixels;
    int width, height;
    int stride_pixels;
};

inline Film film_alloc(aleph::alloc::Arena& arena, int w, int h, int stride = 0) noexcept {
    const int s = (stride > 0) ? stride : w;
    const std::size_t bytes = static_cast<std::size_t>(s) * static_cast<std::size_t>(h)
                              * sizeof(aleph::math::Vec3);
    void* mem = arena.allocate(bytes, alignof(aleph::math::Vec3));
    Film f{};
    f.pixels = static_cast<aleph::math::Vec3*>(mem);
    f.width  = w;
    f.height = h;
    f.stride_pixels = s;
    return f;
}

}  // namespace aleph::render::common
```

- [ ] **Step 10.3: Write `aleph.render.common-tonemap.cppm`**

```cpp
module;
#include <cmath>
#include <algorithm>
#include <cstdint>

export module aleph.render.common:tonemap;

import aleph.math;

export namespace aleph::render::common {

inline std::uint8_t byte_from_linear(aleph::math::f32 x) noexcept {
    const aleph::math::f32 clamped = std::clamp(x, 0.0f, 1.0f);
    const aleph::math::f32 g = std::sqrt(clamped);  // gamma 2.0
    return static_cast<std::uint8_t>(255.999f * g);
}

inline std::uint32_t tonemap_argb8888_gamma2(aleph::math::Vec3 linear) noexcept {
    const std::uint32_t a = 0xFFu;
    const std::uint32_t r = byte_from_linear(linear.x);
    const std::uint32_t g = byte_from_linear(linear.y);
    const std::uint32_t b = byte_from_linear(linear.z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

}  // namespace aleph::render::common
```

- [ ] **Step 10.4: Update primary + wire + build + test + commit**

`aleph.render.common.cppm` adds `export import :film; export import :tonemap;`.
CMake FILE_SET grows. Tests CMakeLists adds the two new test files.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.common/ tests/render/test_film.cpp tests/render/test_tonemap.cpp tests/CMakeLists.txt
git commit -m "task 10: aleph.render.common:film + :tonemap — Arena-backed Film + gamma2 tonemap"
```

---

## Task 11: `aleph.render.rt:material` — scatter dispatch + sampling helpers

**Files:**
- Create: `render/src/aleph.render.rt/CMakeLists.txt`
- Create: `render/src/aleph.render.rt/aleph.render.rt.cppm`
- Create: `render/src/aleph.render.rt/aleph.render.rt-sampling.cppm`
- Create: `render/src/aleph.render.rt/aleph.render.rt-material.cppm`
- Create: `tests/render/test_material_scatter.cpp`
- Modify: `render/CMakeLists.txt` → uncomment `add_subdirectory(src/aleph.render.rt)`

- [ ] **Step 11.1: Write failing test**

```cpp
#include "doctest.h"
#include <optional>
import aleph.render.rt;
import aleph.scene;
import aleph.math;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::math::Ray;
using aleph::math::Pcg32;
using aleph::scene::Scene;
using aleph::scene::MaterialHandle;
using aleph::scene::MaterialKind;
using aleph::scene::HitRecord;

TEST_CASE("scatter Lambertian: produces scattered ray + albedo attenuation") {
    Scene s;
    const auto m = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.3f, 0.7f});
    HitRecord rec{};
    rec.p = Vec3{0, 0, 0};
    rec.normal = Vec3{0, 1, 0};
    rec.front_face = true;
    rec.mat = m;
    Pcg32 rng(42, 0);
    auto r = scatter(s, Ray{Vec3{0, 1, 0}, Vec3{0, -1, 0}}, rec, rng);
    REQUIRE(r.has_value());
    CHECK(r->attenuation == Vec3{0.5f, 0.3f, 0.7f});
    CHECK(r->scattered.origin == Vec3{0, 0, 0});
}

TEST_CASE("scatter Emissive: returns nullopt (no scatter)") {
    Scene s;
    const auto m = aleph::scene::scene_add_emissive(s, Vec3{15, 15, 15});
    HitRecord rec{};
    rec.mat = m;
    Pcg32 rng(0, 0);
    auto r = scatter(s, Ray{}, rec, rng);
    CHECK_FALSE(r.has_value());
}

TEST_CASE("emitted: Emissive returns emit vec, others return zero") {
    Scene s;
    const auto e   = aleph::scene::scene_add_emissive(s, Vec3{15, 15, 15});
    const auto lam = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(emitted(s, e)   == Vec3{15, 15, 15});
    CHECK(emitted(s, lam) == Vec3{0, 0, 0});
}
```

`mkdir -p render/src/aleph.render.rt` if needed.

- [ ] **Step 11.2: Write `aleph.render.rt-sampling.cppm`**

```cpp
export module aleph.render.rt:sampling;

import aleph.math;

export namespace aleph::render::rt {

// Rejection sampling: uniform in unit sphere. ~2 attempts avg.
inline aleph::math::Vec3 rng_in_unit_sphere(aleph::math::Pcg32& r) noexcept {
    for (;;) {
        const aleph::math::Vec3 p{
            r.float01() * 2.0f - 1.0f,
            r.float01() * 2.0f - 1.0f,
            r.float01() * 2.0f - 1.0f,
        };
        if (aleph::math::length_sq(p) < 1.0f) return p;
    }
}

inline aleph::math::Vec3 rng_unit_vec3(aleph::math::Pcg32& r) noexcept {
    return aleph::math::normalize(rng_in_unit_sphere(r));
}

}  // namespace aleph::render::rt
```

- [ ] **Step 11.3: Write `aleph.render.rt-material.cppm`**

```cpp
module;
#include <cmath>
#include <optional>

export module aleph.render.rt:material;

import aleph.math;
import aleph.scene;
import :sampling;

export namespace aleph::render::rt {

struct ScatterResult {
    aleph::math::Ray  scattered;
    aleph::math::Vec3 attenuation;
};

namespace detail {

inline aleph::math::Vec3 sample_textured_albedo(const aleph::scene::Scene& s,
                                                  std::uint32_t mat_idx,
                                                  aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const std::uint32_t tex_id   = s.tex_lamb.tex_id[mat_idx];
    const aleph::math::Vec2 uvs  = s.tex_lamb.uv_scale[mat_idx];
    return aleph::common::sample_bilinear(s.textures[tex_id], u * uvs.x, v * uvs.y);
}

// Schlick Fresnel approximation.
inline aleph::math::f32 schlick_reflectance(aleph::math::f32 cosine,
                                              aleph::math::f32 ref_idx) noexcept {
    aleph::math::f32 r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    const aleph::math::f32 oc = 1.0f - cosine;
    return r0 + (1.0f - r0) * oc * oc * oc * oc * oc;
}

}  // namespace detail

[[nodiscard]] inline std::optional<ScatterResult>
scatter(const aleph::scene::Scene& s,
        aleph::math::Ray in,
        const aleph::scene::HitRecord& rec,
        aleph::math::Pcg32& rng) noexcept {
    const auto& m = rec.mat;
    switch (m.kind) {
        case aleph::scene::MaterialKind::Lambertian: {
            aleph::math::Vec3 dir = rec.normal + rng_unit_vec3(rng);
            if (aleph::math::near_zero(dir)) dir = rec.normal;
            return ScatterResult{ aleph::math::Ray{rec.p, dir}, s.lamb.albedo[m.idx] };
        }
        case aleph::scene::MaterialKind::Metal: {
            const aleph::math::Vec3 unit      = aleph::math::normalize(in.dir);
            const aleph::math::Vec3 reflected = aleph::math::reflect(unit, rec.normal);
            const aleph::math::Vec3 scat = reflected + rng_in_unit_sphere(rng) * s.metal.fuzz[m.idx];
            if (aleph::math::dot(scat, rec.normal) <= 0.0f) return std::nullopt;
            return ScatterResult{ aleph::math::Ray{rec.p, scat}, s.metal.albedo[m.idx] };
        }
        case aleph::scene::MaterialKind::Dielectric: {
            const aleph::math::f32 ior = s.diel.ior[m.idx];
            const aleph::math::Vec3 unit = aleph::math::normalize(in.dir);
            const aleph::math::f32 ri = rec.front_face ? (1.0f / ior) : ior;
            aleph::math::f32 cos_t = aleph::math::dot(-unit, rec.normal);
            if (cos_t > 1.0f) cos_t = 1.0f;
            const aleph::math::f32 sin_t = std::sqrt(1.0f - cos_t*cos_t);
            const bool tir = (ri * sin_t) > 1.0f;
            const bool refl = tir || (detail::schlick_reflectance(cos_t, ri) > rng.float01());
            const aleph::math::Vec3 dir = refl ? aleph::math::reflect(unit, rec.normal)
                                                : aleph::math::refract(unit, rec.normal, ri);
            return ScatterResult{ aleph::math::Ray{rec.p, dir}, aleph::math::Vec3{1, 1, 1} };
        }
        case aleph::scene::MaterialKind::Emissive:
            return std::nullopt;   // emit only, no scatter
        case aleph::scene::MaterialKind::TexturedLambertian: {
            aleph::math::Vec3 dir = rec.normal + rng_unit_vec3(rng);
            if (aleph::math::near_zero(dir)) dir = rec.normal;
            return ScatterResult{ aleph::math::Ray{rec.p, dir},
                                  detail::sample_textured_albedo(s, m.idx, rec.u, rec.v) };
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline aleph::math::Vec3
emitted(const aleph::scene::Scene& s, aleph::scene::MaterialHandle m) noexcept {
    if (m.kind == aleph::scene::MaterialKind::Emissive) return s.emis.emit[m.idx];
    return aleph::math::Vec3{};
}

}  // namespace aleph::render::rt
```

- [ ] **Step 11.4: Primary + CMake + wire**

`aleph.render.rt.cppm`:
```cpp
export module aleph.render.rt;
export import :sampling;
export import :material;
```

`render/src/aleph.render.rt/CMakeLists.txt`:
```cmake
add_library(aleph_render_rt)
target_sources(aleph_render_rt
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.rt.cppm
        aleph.render.rt-sampling.cppm
        aleph.render.rt-material.cppm)
target_link_libraries(aleph_render_rt
    PUBLIC  aleph_scene aleph_render_common aleph_math aleph_threads
    PRIVATE aleph_flags_isa)
```

Uncomment in `render/CMakeLists.txt`. Add `render/test_material_scatter.cpp` + `aleph_render_rt` to tests CMakeLists.

- [ ] **Step 11.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.rt/ render/CMakeLists.txt tests/render/test_material_scatter.cpp tests/CMakeLists.txt
git commit -m "task 11: aleph.render.rt:sampling + :material — scatter dispatch over MaterialKind"
```

---

## Task 12: `aleph.render.rt:integrator` — `ray_color` with NEE

**Files:**
- Create: `render/src/aleph.render.rt/aleph.render.rt-integrator.cppm`
- Create: `tests/render/test_ray_color.cpp`

- [ ] **Step 12.1: Write failing test**

```cpp
#include "doctest.h"
#include <limits>
import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::math::Ray;

TEST_CASE("ray_color: hit sphere returns scaled albedo (1 bounce) or sky") {
    aleph::scene::Scene s;
    const auto m = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    aleph::scene::scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    alignas(16) static unsigned char scratch[4096];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);

    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};
    aleph::math::Pcg32 rng(42, 0);
    // Ray missing the sphere goes to sky.
    Vec3 c_miss = ray_color(s, Ray{Vec3{0, 10, 0}, Vec3{0, 1, 0}}, 5, sky, true, rng);
    // sky.high at +y → both pure +y → exactly sky.high
    CHECK(c_miss == sky.high);
}

TEST_CASE("ray_color depth=0 returns black") {
    aleph::scene::Scene s;
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{1, 1, 1}};
    aleph::math::Pcg32 rng(0, 0);
    Vec3 c = ray_color(s, Ray{Vec3{0, 0, 0}, Vec3{0, 1, 0}}, 0, sky, true, rng);
    CHECK(c == Vec3{0, 0, 0});
}
```

- [ ] **Step 12.2: Write `aleph.render.rt-integrator.cppm`**

```cpp
module;
#include <cmath>
#include <limits>

export module aleph.render.rt:integrator;

import aleph.math;
import aleph.scene;
import aleph.render.common;
import :sampling;
import :material;

export namespace aleph::render::rt {

namespace detail {

// Direct light contribution from a single quad-emitter. Returns black if
// occluded or geometry is back-to-back. Mirrors Sotark cxx26 NEE algorithm.
inline aleph::math::Vec3 direct_light_quad(
    const aleph::scene::Scene& s,
    aleph::scene::Handle32 light_h,
    aleph::math::Vec3 hit_p, aleph::math::Vec3 hit_normal,
    aleph::math::Vec3 surf_albedo,
    aleph::math::Pcg32& rng) noexcept
{
    const std::uint32_t li = light_h.index();
    // Sample a uniform point in the emissive quad.
    const aleph::math::f32 ra = rng.float01();
    const aleph::math::f32 rb = rng.float01();
    const aleph::math::Vec3 Q{s.quads.Qx[li], s.quads.Qy[li], s.quads.Qz[li]};
    const aleph::math::Vec3 u_edge{s.quads.ux[li], s.quads.uy[li], s.quads.uz[li]};
    const aleph::math::Vec3 v_edge{s.quads.vx[li], s.quads.vy[li], s.quads.vz[li]};
    const aleph::math::Vec3 P = Q + u_edge * ra + v_edge * rb;
    const aleph::math::Vec3 to_light = P - hit_p;
    const aleph::math::f32 dist_sq = aleph::math::length_sq(to_light);
    if (dist_sq < 1e-6f) return aleph::math::Vec3{};
    const aleph::math::f32 dist = std::sqrt(dist_sq);
    const aleph::math::f32 cos_theta = aleph::math::dot(hit_normal, to_light) / dist;
    if (cos_theta <= 0.0f) return aleph::math::Vec3{};
    const aleph::math::Vec3 n_light{s.quads.nx[li], s.quads.ny[li], s.quads.nz[li]};
    const aleph::math::f32 cos_alpha = -aleph::math::dot(n_light, to_light) / dist;
    if (cos_alpha <= 0.0f) return aleph::math::Vec3{};
    // Shadow test — hit anything? If it's not the light's own material, occluded.
    auto srec = aleph::scene::hit(s, aleph::math::Ray{hit_p, to_light}, 0.001f, 1.001f);
    if (!srec || srec->mat.kind != aleph::scene::MaterialKind::Emissive)
        return aleph::math::Vec3{};
    // Light contribution: cos_theta * cos_alpha * area / (dist² * π) * (albedo * emit).
    const aleph::math::f32 area = aleph::math::length(aleph::math::cross(u_edge, v_edge));
    const aleph::math::Vec3 emit = s.emis.emit[srec->mat.idx];
    const aleph::math::f32 factor = cos_theta * cos_alpha * area /
                                    (dist_sq * aleph::math::pi_f);
    return aleph::math::Vec3{
        surf_albedo.x * emit.x, surf_albedo.y * emit.y, surf_albedo.z * emit.z
    } * factor;
}

}  // namespace detail

[[nodiscard]] inline aleph::math::Vec3
ray_color(const aleph::scene::Scene& scene, aleph::math::Ray r, int depth,
           aleph::render::common::Sky sky, bool include_emission,
           aleph::math::Pcg32& rng) noexcept {
    if (depth <= 0) return aleph::math::Vec3{};

    auto rec_opt = aleph::scene::hit(scene, r, 0.001f,
                                       std::numeric_limits<aleph::math::f32>::infinity());
    if (!rec_opt) {
        const aleph::math::Vec3 unit = aleph::math::normalize(r.dir);
        return aleph::render::common::sky_sample(sky, unit);
    }
    const auto& rec = *rec_opt;

    const aleph::math::Vec3 emit_v = include_emission ? emitted(scene, rec.mat)
                                                       : aleph::math::Vec3{};

    if (rec.mat.kind == aleph::scene::MaterialKind::Emissive) return emit_v;

    // NEE on Lambertian (incl. TexturedLambertian for the same BRDF).
    aleph::math::Vec3 direct{};
    const bool is_diffuse = rec.mat.kind == aleph::scene::MaterialKind::Lambertian
                          || rec.mat.kind == aleph::scene::MaterialKind::TexturedLambertian;
    if (is_diffuse && !scene.lights.empty()) {
        const aleph::math::Vec3 surf_albedo =
            (rec.mat.kind == aleph::scene::MaterialKind::Lambertian)
                ? scene.lamb.albedo[rec.mat.idx]
                : detail::sample_textured_albedo(scene, rec.mat.idx, rec.u, rec.v);
        for (auto Lh : scene.lights) {
            if (Lh.hittable_kind() != aleph::scene::HittableKind::Quad) continue;
            direct = direct + detail::direct_light_quad(scene, Lh, rec.p, rec.normal, surf_albedo, rng);
        }
    }

    auto scat = scatter(scene, r, rec, rng);
    if (!scat) return emit_v + direct;

    const bool nee_done = is_diffuse && !scene.lights.empty();
    const aleph::math::Vec3 indirect = ray_color(scene, scat->scattered, depth - 1,
                                                   sky, !nee_done, rng);
    return emit_v + direct
         + aleph::math::Vec3{
               scat->attenuation.x * indirect.x,
               scat->attenuation.y * indirect.y,
               scat->attenuation.z * indirect.z,
           };
}

}  // namespace aleph::render::rt
```

**Note:** `detail::sample_textured_albedo` is defined in `:material` (Task 11). Since `:integrator` imports `:material`, the function should be reachable. If linker complains about the `detail` ns not being exported, **move** `sample_textured_albedo` from `:material`'s `namespace detail` into the public namespace as `aleph::render::rt::sample_textured_albedo` (it's already an internal helper).

- [ ] **Step 12.3: Wire + build + test + commit**

`aleph.render.rt.cppm` adds `export import :integrator;`. CMake FILE_SET grows.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.rt/aleph.render.rt-integrator.cppm render/src/aleph.render.rt/aleph.render.rt-material.cppm render/src/aleph.render.rt/aleph.render.rt.cppm render/src/aleph.render.rt/CMakeLists.txt tests/render/test_ray_color.cpp tests/CMakeLists.txt
git commit -m "task 12: aleph.render.rt:integrator — ray_color with NEE on diffuse surfaces"
```

---

## Task 13: `aleph.render.rt:path_trace` — tile dispatcher

**Files:**
- Create: `render/src/aleph.render.rt/aleph.render.rt-path_trace.cppm`
- Create: `tests/render/test_path_trace.cpp`

- [ ] **Step 13.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::rt;
using aleph::math::Vec3;

TEST_CASE("path_trace: 16x16 cover scene → film has non-zero pixels") {
    aleph::scene::Scene s;
    const auto m_ground = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    aleph::scene::scene_add_sphere(s, Vec3{0, -1000, 0}, 1000.0f, m_ground);
    aleph::scene::scene_add_sphere(s, Vec3{0, 1, 0}, 1.0f, m_ground);
    alignas(16) static unsigned char scratch[16 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);

    alignas(64) static unsigned char film_buf[16 * 1024];
    aleph::alloc::Arena film_arena{film_buf, sizeof(film_buf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, 16, 16);

    const aleph::render::common::Camera cam =
        aleph::render::common::make_camera(Vec3{13, 2, 3}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                                            20.0f, 16, 16, 0.0f, 10.0f);
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};

    aleph::threads::Pool pool(2);
    path_trace(s, cam, sky, film, pool, RenderOpts{4, 5, 42, 8});
    // At least one pixel should be non-black.
    int non_black = 0;
    for (int i = 0; i < film.width * film.height; ++i) {
        if (aleph::math::length_sq(film.pixels[i]) > 0.0f) ++non_black;
    }
    CHECK(non_black > 0);
}
```

- [ ] **Step 13.2: Write `aleph.render.rt-path_trace.cppm`**

```cpp
module;
#include <atomic>
#include <cstdint>
#include <algorithm>

export module aleph.render.rt:path_trace;

import aleph.math;
import aleph.scene;
import aleph.render.common;
import aleph.threads;
import :integrator;

export namespace aleph::render::rt {

struct RenderOpts {
    int spp{16};
    int max_depth{8};
    aleph::math::u64 base_seed{42};
    int tile_size{32};
};

namespace detail {

inline void render_tile(const aleph::scene::Scene& scene,
                          const aleph::render::common::Camera& cam,
                          aleph::render::common::Sky sky,
                          aleph::render::common::Film& film,
                          int tile_idx, int tiles_x,
                          const RenderOpts& opts) noexcept {
    const int tx = tile_idx % tiles_x;
    const int ty = tile_idx / tiles_x;
    const int x0 = tx * opts.tile_size;
    const int y0 = ty * opts.tile_size;
    const int x1 = std::min(x0 + opts.tile_size, film.width);
    const int y1 = std::min(y0 + opts.tile_size, film.height);
    aleph::math::Pcg32 rng(opts.base_seed, static_cast<aleph::math::u64>(tile_idx) + 1ull);
    const aleph::math::f32 inv_spp = 1.0f / static_cast<aleph::math::f32>(opts.spp);
    for (int j = y0; j < y1; ++j) {
        for (int i = x0; i < x1; ++i) {
            aleph::math::Vec3 accum{};
            for (int s = 0; s < opts.spp; ++s) {
                const aleph::math::Ray r = aleph::render::common::camera_get_ray(cam, i, j, rng);
                accum = accum + ray_color(scene, r, opts.max_depth, sky, true, rng);
            }
            film.pixels[j * film.stride_pixels + i] = accum * inv_spp;
        }
    }
}

}  // namespace detail

inline void path_trace(const aleph::scene::Scene& scene,
                        const aleph::render::common::Camera& cam,
                        aleph::render::common::Sky sky,
                        aleph::render::common::Film& film,
                        aleph::threads::Pool& pool,
                        RenderOpts opts) noexcept {
    const int tiles_x = (film.width  + opts.tile_size - 1) / opts.tile_size;
    const int tiles_y = (film.height + opts.tile_size - 1) / opts.tile_size;
    const int total = tiles_x * tiles_y;
    pool.parallel_for(0, total, [&](int t) {
        detail::render_tile(scene, cam, sky, film, t, tiles_x, opts);
    });
}

}  // namespace aleph::render::rt
```

- [ ] **Step 13.3: Wire + build + test + commit**

`aleph.render.rt.cppm` adds `export import :path_trace;`. CMake FILE_SET grows.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.rt/aleph.render.rt-path_trace.cppm render/src/aleph.render.rt/aleph.render.rt.cppm render/src/aleph.render.rt/CMakeLists.txt tests/render/test_path_trace.cpp tests/CMakeLists.txt
git commit -m "task 13: aleph.render.rt:path_trace — tile dispatcher via aleph.threads::Pool; rt module complete"
```

---

## Task 14: `aleph.render.sw:clip` + `:scene_rt` — Sutherland-Hodgman + Face/SceneRT data

**Files:**
- Create: `render/src/aleph.render.sw/CMakeLists.txt`
- Create: `render/src/aleph.render.sw/aleph.render.sw.cppm`
- Create: `render/src/aleph.render.sw/aleph.render.sw-clip.cppm`
- Create: `render/src/aleph.render.sw/aleph.render.sw-scene_rt.cppm`
- Create: `tests/render/test_sw_clip.cpp`
- Modify: `render/CMakeLists.txt` (uncomment add_subdirectory)

- [ ] **Step 14.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.render.sw;
import aleph.math;

using namespace aleph::render::sw;

TEST_CASE("clip_triangle_near: all 3 verts in front → 1 unmodified tri") {
    ClipVert a{0, 0, 0, 1.0f, 0, 0};
    ClipVert b{1, 0, 0, 1.0f, 1, 0};
    ClipVert c{0, 1, 0, 1.0f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(a, b, c, 0.1f, out);
    CHECK(n == 1);
    CHECK(out[0].w == 1.0f);
    CHECK(out[1].w == 1.0f);
    CHECK(out[2].w == 1.0f);
}

TEST_CASE("clip_triangle_near: all 3 behind → 0 tris") {
    ClipVert a{0, 0, 0, 0.05f, 0, 0};
    ClipVert b{1, 0, 0, 0.05f, 1, 0};
    ClipVert c{0, 1, 0, 0.05f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(a, b, c, 0.1f, out);
    CHECK(n == 0);
}

TEST_CASE("clip_triangle_near: 1 in, 2 out → 1 sub-tri") {
    ClipVert in {0, 0, 0, 1.0f,  0, 0};
    ClipVert o1{1, 0, 0, 0.05f, 1, 0};
    ClipVert o2{0, 1, 0, 0.05f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(in, o1, o2, 0.1f, out);
    CHECK(n == 1);
    // All resulting verts should have w >= 0.1.
    for (int i = 0; i < 3; ++i) CHECK(out[i].w >= 0.1f);
}
```

`mkdir -p render/src/aleph.render.sw` if needed.

- [ ] **Step 14.2: Write `aleph.render.sw-clip.cppm`**

```cpp
module;
#include <array>

export module aleph.render.sw:clip;

import aleph.math;

export namespace aleph::render::sw {

struct ClipVert {
    aleph::math::f32 x, y, z, w;
    aleph::math::f32 u, v;
};

struct ScreenVert {
    aleph::math::f32 x, y, z;
    aleph::math::f32 u, v, inv_w;
};

namespace detail {

inline ClipVert lerp_clip(ClipVert a, ClipVert b, aleph::math::f32 t) noexcept {
    return ClipVert{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
        a.u + (b.u - a.u) * t,
        a.v + (b.v - a.v) * t,
    };
}

}  // namespace detail

inline int clip_triangle_near(ClipVert a, ClipVert b, ClipVert c,
                                aleph::math::f32 near_w,
                                std::array<ClipVert, 6>& out) noexcept {
    const std::array<ClipVert, 3> verts{a, b, c};
    const std::array<int, 3> ins{
        a.w >= near_w ? 1 : 0,
        b.w >= near_w ? 1 : 0,
        c.w >= near_w ? 1 : 0,
    };
    const int n_in = ins[0] + ins[1] + ins[2];
    if (n_in == 0) return 0;
    if (n_in == 3) { out[0] = a; out[1] = b; out[2] = c; return 1; }

    std::array<ClipVert, 4> poly{};
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        if (ins[i]) poly[n++] = verts[i];
        if (ins[i] != ins[j]) {
            const aleph::math::f32 t =
                (near_w - verts[i].w) / (verts[j].w - verts[i].w);
            poly[n] = detail::lerp_clip(verts[i], verts[j], t);
            poly[n].w = near_w;
            ++n;
        }
    }
    int n_tris = 0;
    for (int i = 1; i < n - 1; ++i) {
        out[n_tris * 3 + 0] = poly[0];
        out[n_tris * 3 + 1] = poly[i];
        out[n_tris * 3 + 2] = poly[i + 1];
        ++n_tris;
    }
    return n_tris;
}

}  // namespace aleph::render::sw
```

- [ ] **Step 14.3: Write `aleph.render.sw-scene_rt.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>
#include <array>

export module aleph.render.sw:scene_rt;

import aleph.math;

export namespace aleph::render::sw {

// Texture sampler: (u, v) → ARGB. Replaces Sotark's tex_sample_fn.
using TexSampleFn = aleph::math::u32 (*)(aleph::math::f32, aleph::math::f32);

struct Lightmap {
    aleph::math::u32* texels;        // points into SceneRT::lightmap_pool
    int w, h;
    aleph::math::f32 u_min, u_max, v_min, v_max;
};

struct Face {
    std::array<aleph::math::Vec3, 4> verts;
    std::array<aleph::math::Vec2, 4> uvs;
    TexSampleFn  tex;
    aleph::math::u32 lightmap_id;     // index into SceneRT::lightmaps; 0xFFFFFFFFu = no lightmap
};

struct SceneRT {
    std::vector<Face>            faces;
    std::vector<Lightmap>        lightmaps;
    // One contiguous backing array for all lightmap texels; sliced via Lightmap::texels.
    std::vector<aleph::math::u32> lightmap_pool;
};

// Built-in procedural texture samplers (port from Sotark sw-texture).
aleph::math::u32 tex_checker(aleph::math::f32 u, aleph::math::f32 v) noexcept;
aleph::math::u32 tex_brick  (aleph::math::f32 u, aleph::math::f32 v) noexcept;
aleph::math::u32 tex_floor  (aleph::math::f32 u, aleph::math::f32 v) noexcept;
aleph::math::u32 tex_ceiling(aleph::math::f32 u, aleph::math::f32 v) noexcept;

}  // namespace aleph::render::sw
```

Define the four `tex_*` functions in the same file (ported verbatim from
Sotark cxx26's `sotark.sw-texture.cppm`):

```cpp
inline aleph::math::u32 tex_checker(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const int cu = static_cast<int>(std::floor(u));
    const int cv = static_cast<int>(std::floor(v));
    return ((cu ^ cv) & 1) ? 0xFFE0E0E0u : 0xFF303030u;
}

inline aleph::math::u32 tex_brick(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const int row = static_cast<int>(std::floor(v));
    const aleph::math::f32 u_off = (row & 1) ? 1.0f : 0.0f;
    const aleph::math::f32 uu = u + u_off;
    const aleph::math::f32 fu = uu - std::floor(uu * 0.5f) * 2.0f;
    const aleph::math::f32 fv = v - std::floor(v);
    if (fu < 0.08f || fu > 1.92f || fv < 0.08f || fv > 0.92f) return 0xFF555555u;
    const int col = static_cast<int>(std::floor(uu * 0.5f));
    const aleph::math::u32 n =
        static_cast<aleph::math::u32>((col * 73856093) ^ (row * 19349663));
    const aleph::math::u8 r = 140u + static_cast<aleph::math::u8>((n >> 16) & 31);
    const aleph::math::u8 g =  70u + static_cast<aleph::math::u8>((n >>  8) & 15);
    const aleph::math::u8 b =  50u + static_cast<aleph::math::u8>( n        & 15);
    return 0xFF000000u | (static_cast<aleph::math::u32>(r) << 16)
         | (static_cast<aleph::math::u32>(g) << 8) | b;
}

inline aleph::math::u32 tex_floor(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::f32 fu = u * 2.0f;
    const aleph::math::f32 fv = v * 2.0f;
    const int cu = static_cast<int>(std::floor(fu));
    const int cv = static_cast<int>(std::floor(fv));
    const aleph::math::f32 lu = fu - std::floor(fu);
    const aleph::math::f32 lv = fv - std::floor(fv);
    if (lu < 0.04f || lu > 0.96f || lv < 0.04f || lv > 0.96f) return 0xFF202020u;
    return ((cu ^ cv) & 1) ? 0xFFB0B0B0u : 0xFF909090u;
}

inline aleph::math::u32 tex_ceiling(aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::u32 n =
        static_cast<aleph::math::u32>(static_cast<int>(std::floor(u * 4.0f)) * 73856093)
        ^ static_cast<aleph::math::u32>(static_cast<int>(std::floor(v * 4.0f)) * 19349663);
    const aleph::math::u8 r = 200u + static_cast<aleph::math::u8>((n >> 16) & 15);
    const aleph::math::u8 g = 195u + static_cast<aleph::math::u8>((n >>  8) & 15);
    const aleph::math::u8 b = 180u + static_cast<aleph::math::u8>( n        & 15);
    return 0xFF000000u | (static_cast<aleph::math::u32>(r) << 16)
         | (static_cast<aleph::math::u32>(g) << 8) | b;
}
```

Also need `#include <cmath>` in the global module fragment for `std::floor`.

- [ ] **Step 14.4: Primary + CMake + wire**

`aleph.render.sw.cppm`:
```cpp
export module aleph.render.sw;
export import :clip;
export import :scene_rt;
```

`render/src/aleph.render.sw/CMakeLists.txt`:
```cmake
add_library(aleph_render_sw)
target_sources(aleph_render_sw
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.sw.cppm
        aleph.render.sw-clip.cppm
        aleph.render.sw-scene_rt.cppm)
target_link_libraries(aleph_render_sw
    PUBLIC  aleph_render_common aleph_math aleph_threads
    PRIVATE aleph_flags_isa)
```

Uncomment in `render/CMakeLists.txt`. Add `aleph_render_sw` to test link libs and `render/test_sw_clip.cpp` to test sources.

- [ ] **Step 14.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.sw/ render/CMakeLists.txt tests/render/test_sw_clip.cpp tests/CMakeLists.txt
git commit -m "task 14: aleph.render.sw:clip + :scene_rt — Sutherland-Hodgman + Face/SceneRT + procedural textures"
```

---

## Task 15: `aleph.render.sw:span_buffer` + scanline subspan rasterize

**Files:**
- Create: `render/src/aleph.render.sw/aleph.render.sw-span_buffer.cppm`
- Create: `render/src/aleph.render.sw/aleph.render.sw-rast_scan.cppm`
- Create: `tests/render/test_sw_span_buffer.cpp`
- Create: `tests/render/test_sw_rasterize.cpp`

- [ ] **Step 15.1: Write failing tests**

`test_sw_span_buffer.cpp`:
```cpp
#include "doctest.h"
import aleph.render.sw;

using namespace aleph::render::sw;

TEST_CASE("SpanBuffer: empty → emit one full span → drawn count == width") {
    SpanBuffer sb(16, 1);
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 16);
    CHECK(sb.pixels_drawn() == 16);
}

TEST_CASE("SpanBuffer: second emit on same row → drawn 0 (already covered)") {
    SpanBuffer sb(16, 1);
    sb.emit(0, 0, 16, [](int, int, int){});
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 0);
    CHECK(sb.pixels_skipped() >= 16);
}
```

`test_sw_rasterize.cpp`:
```cpp
#include "doctest.h"
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
import aleph.alloc;

using namespace aleph::render::sw;

TEST_CASE("rast_scan_textured: writes some pixels for a centered triangle") {
    alignas(64) static unsigned char fbuf[16 * 16 * 16];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film color = aleph::render::common::film_alloc(arena, 16, 16);
    // Clear film to magenta sentinel
    for (int i = 0; i < 16 * 16; ++i) color.pixels[i] = aleph::math::Vec3{1, 0, 1};
    std::vector<aleph::math::f32> depth(16 * 16, 1.0f);
    SpanBuffer sb(16, 16);

    ScreenVert v0{2.0f,  2.0f, 0.0f, 0, 0, 1.0f};
    ScreenVert v1{14.0f, 2.0f, 0.0f, 1, 0, 1.0f};
    ScreenVert v2{8.0f, 14.0f, 0.0f, 0, 1, 1.0f};

    rast_scan_textured(color, depth, sb, v0, v1, v2,
                        tex_floor, nullptr, 0, 16);

    int touched = 0;
    for (int i = 0; i < 16 * 16; ++i)
        if (!(color.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    CHECK(touched > 0);
}
```

- [ ] **Step 15.2: Write `aleph.render.sw-span_buffer.cppm`**

```cpp
module;
#include <cstdint>
#include <vector>
#include <functional>
#include <algorithm>

export module aleph.render.sw:span_buffer;

import aleph.math;

export namespace aleph::render::sw {

using SpanDrawFn = std::function<void(int y, int x0, int x1)>;

class SpanBuffer {
public:
    SpanBuffer() = default;
    SpanBuffer(int w, int h)
        : covered_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0u),
          w_{w}, h_{h} {}

    void clear() noexcept {
        std::fill(covered_.begin(), covered_.end(), aleph::math::u8{0});
        pixels_drawn_ = 0;
        pixels_skipped_ = 0;
    }

    void emit(int y, int x0, int x1, const SpanDrawFn& fn) {
        if (y < 0 || y >= h_) return;
        if (x0 < 0) x0 = 0;
        if (x1 > w_) x1 = w_;
        if (x0 >= x1) return;

        aleph::math::u8* row = covered_.data() + static_cast<std::size_t>(y) * w_;
        int run_start = -1;
        for (int x = x0; x < x1; ++x) {
            if (!row[x]) {
                if (run_start < 0) run_start = x;
                row[x] = 1;
            } else {
                ++pixels_skipped_;
                if (run_start >= 0) {
                    fn(y, run_start, x);
                    pixels_drawn_ += x - run_start;
                    run_start = -1;
                }
            }
        }
        if (run_start >= 0) {
            fn(y, run_start, x1);
            pixels_drawn_ += x1 - run_start;
        }
    }

    int pixels_drawn()   const noexcept { return pixels_drawn_; }
    int pixels_skipped() const noexcept { return pixels_skipped_; }

private:
    std::vector<aleph::math::u8> covered_;
    int w_{0}, h_{0};
    int pixels_drawn_{0};
    int pixels_skipped_{0};
};

}  // namespace aleph::render::sw
```

- [ ] **Step 15.3: Write `aleph.render.sw-rast_scan.cppm`**

Port of Sotark cxx26's `sotark.sw-rast_scan.cppm` adapted to write into a `Film`
(Vec3 pixels) instead of `u32 ARGB`. This implementation is scalar subspan FDIV
(AVX2 batched variant is deferred per the spec):

```cpp
module;
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

export module aleph.render.sw:rast_scan;

import aleph.math;
import aleph.render.common;
import :clip;
import :scene_rt;
import :span_buffer;

export namespace aleph::render::sw {

namespace detail {

inline aleph::math::Vec3 argb_to_linear(aleph::math::u32 argb) noexcept {
    const aleph::math::f32 r = static_cast<aleph::math::f32>((argb >> 16) & 0xFFu) / 255.0f;
    const aleph::math::f32 g = static_cast<aleph::math::f32>((argb >>  8) & 0xFFu) / 255.0f;
    const aleph::math::f32 b = static_cast<aleph::math::f32>( argb        & 0xFFu) / 255.0f;
    return aleph::math::Vec3{r, g, b};
}

}  // namespace detail

// Single-triangle rasterizer with subspan FDIV (Abrash-era trick).
// Writes into the Film (Vec3 linear color) and depth buffer.
// `lm` may be nullptr for unmodulated samples.
inline void rast_scan_textured(aleph::render::common::Film& fb,
                                std::span<aleph::math::f32> depth,
                                SpanBuffer& sb,
                                ScreenVert v0, ScreenVert v1, ScreenVert v2,
                                TexSampleFn tex,
                                const Lightmap* lm,
                                int y_clip_min, int y_clip_max) noexcept {
    // Backface cull: Y-flipped → front faces have NEGATIVE signed area.
    const aleph::math::f32 signed_area =
        (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
    if (signed_area > 0.0f) return;

    if (v0.y > v1.y) std::swap(v0, v1);
    if (v1.y > v2.y) std::swap(v1, v2);
    if (v0.y > v1.y) std::swap(v0, v1);

    const aleph::math::f32 dx10 = v1.x - v0.x, dy10 = v1.y - v0.y;
    const aleph::math::f32 dx20 = v2.x - v0.x, dy20 = v2.y - v0.y;
    const aleph::math::f32 det = dx10 * dy20 - dy10 * dx20;
    if (std::abs(det) < 1e-6f) return;
    const aleph::math::f32 inv_det = 1.0f / det;

    const aleph::math::f32 iw0 = v0.inv_w, iw1 = v1.inv_w, iw2 = v2.inv_w;
    const aleph::math::f32 uw0 = v0.u * iw0, uw1 = v1.u * iw1, uw2 = v2.u * iw2;
    const aleph::math::f32 vw0 = v0.v * iw0, vw1 = v1.v * iw1, vw2 = v2.v * iw2;

    struct Grad {
        aleph::math::f32 inv_w_dx, inv_w_dy, inv_w_origin;
        aleph::math::f32 u_w_dx,   u_w_dy,   u_w_origin;
        aleph::math::f32 v_w_dx,   v_w_dy,   v_w_origin;
    } g{};

    auto compute_grad = [&](aleph::math::f32 a0, aleph::math::f32 a1, aleph::math::f32 a2,
                              aleph::math::f32& dx, aleph::math::f32& dy, aleph::math::f32& org) {
        const aleph::math::f32 d10 = a1 - a0;
        const aleph::math::f32 d20 = a2 - a0;
        dx  = (d10 * dy20 - d20 * dy10) * inv_det;
        dy  = (d20 * dx10 - d10 * dx20) * inv_det;
        org = a0 - dx * v0.x - dy * v0.y;
    };
    compute_grad(iw0, iw1, iw2, g.inv_w_dx, g.inv_w_dy, g.inv_w_origin);
    compute_grad(uw0, uw1, uw2, g.u_w_dx,   g.u_w_dy,   g.u_w_origin);
    compute_grad(vw0, vw1, vw2, g.v_w_dx,   g.v_w_dy,   g.v_w_origin);

    const int y_top = static_cast<int>(std::ceil(v0.y));
    const int y_bot = static_cast<int>(std::ceil(v2.y));
    const int y_mid = static_cast<int>(std::ceil(v1.y));
    const aleph::math::f32 dy_long  = v2.y - v0.y;
    const aleph::math::f32 dxdy_long  = (dy_long  > 1e-6f) ? (v2.x - v0.x) / dy_long  : 0.0f;
    const aleph::math::f32 dy_upper = v1.y - v0.y;
    const aleph::math::f32 dxdy_upper = (dy_upper > 1e-6f) ? (v1.x - v0.x) / dy_upper : 0.0f;
    const aleph::math::f32 dy_lower = v2.y - v1.y;
    const aleph::math::f32 dxdy_lower = (dy_lower > 1e-6f) ? (v2.x - v1.x) / dy_lower : 0.0f;

    const int y_start = std::max({y_top, y_clip_min, 0});
    const int y_end   = std::min({y_bot, y_clip_max, fb.height});

    constexpr int SUBSPAN = 16;

    for (int y = y_start; y < y_end; ++y) {
        const aleph::math::f32 yc = static_cast<aleph::math::f32>(y) + 0.5f;
        const aleph::math::f32 x_long = v0.x + (yc - v0.y) * dxdy_long;
        aleph::math::f32 x_short;
        if (y < y_mid && dy_upper > 1e-6f) x_short = v0.x + (yc - v0.y) * dxdy_upper;
        else if (dy_lower > 1e-6f)         x_short = v1.x + (yc - v1.y) * dxdy_lower;
        else continue;

        const aleph::math::f32 xl_f = std::min(x_long, x_short);
        const aleph::math::f32 xr_f = std::max(x_long, x_short);
        const int xl = static_cast<int>(std::ceil(xl_f));
        const int xr = static_cast<int>(std::ceil(xr_f));
        if (xl >= xr) continue;

        sb.emit(y, xl, xr, [&](int yy, int x0, int x1) {
            const aleph::math::f32 xs = static_cast<aleph::math::f32>(x0) + 0.5f;
            const aleph::math::f32 ys = static_cast<aleph::math::f32>(yy) + 0.5f;
            aleph::math::f32 inv_w_acc = g.inv_w_origin + xs * g.inv_w_dx + ys * g.inv_w_dy;
            aleph::math::f32 u_w_acc   = g.u_w_origin   + xs * g.u_w_dx   + ys * g.u_w_dy;
            aleph::math::f32 v_w_acc   = g.v_w_origin   + xs * g.v_w_dx   + ys * g.v_w_dy;
            aleph::math::f32 tu_left = u_w_acc / inv_w_acc;
            aleph::math::f32 tv_left = v_w_acc / inv_w_acc;

            int x = x0;
            while (x < x1) {
                int x_next = std::min(x + SUBSPAN, x1);
                const int len = x_next - x;
                const aleph::math::f32 len_f = static_cast<aleph::math::f32>(len);
                inv_w_acc += g.inv_w_dx * len_f;
                u_w_acc   += g.u_w_dx   * len_f;
                v_w_acc   += g.v_w_dx   * len_f;
                const aleph::math::f32 w_right = 1.0f / inv_w_acc;
                const aleph::math::f32 tu_right = u_w_acc * w_right;
                const aleph::math::f32 tv_right = v_w_acc * w_right;
                const aleph::math::f32 inv_len = 1.0f / len_f;
                const aleph::math::f32 du = (tu_right - tu_left) * inv_len;
                const aleph::math::f32 dv = (tv_right - tv_left) * inv_len;

                aleph::math::f32 tu = tu_left;
                aleph::math::f32 tv = tv_left;
                for (int i = 0; i < len; ++i) {
                    const aleph::math::u32 base = tex(tu, tv);
                    aleph::math::Vec3 color = detail::argb_to_linear(base);
                    if (lm) {
                        // sample lightmap (bilinear) — implemented inline.
                        // (Detailed bilinear lookup ported in Task 16.)
                        const aleph::math::f32 fu = (tu - lm->u_min) / (lm->u_max - lm->u_min)
                                                    * static_cast<aleph::math::f32>(lm->w - 1);
                        const aleph::math::f32 fv = (tv - lm->v_min) / (lm->v_max - lm->v_min)
                                                    * static_cast<aleph::math::f32>(lm->h - 1);
                        const int iu = std::clamp(static_cast<int>(fu), 0, lm->w - 1);
                        const int iv = std::clamp(static_cast<int>(fv), 0, lm->h - 1);
                        const aleph::math::u32 mod = lm->texels[iv * lm->w + iu];
                        const aleph::math::Vec3 mod_color = detail::argb_to_linear(mod);
                        color = aleph::math::Vec3{
                            color.x * mod_color.x, color.y * mod_color.y, color.z * mod_color.z};
                    }
                    fb.pixels[yy * fb.stride_pixels + x + i] = color;
                    tu += du; tv += dv;
                }
                tu_left = tu_right;
                tv_left = tv_right;
                x = x_next;
            }
        });
    }
    (void)depth;   // depth buffer not yet wired in V1 — span buffer + front-to-back order suffices
}

}  // namespace aleph::render::sw
```

- [ ] **Step 15.4: Wire + build + test + commit**

`aleph.render.sw.cppm` adds `export import :span_buffer; export import :rast_scan;`.
CMake FILE_SET grows.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.sw/aleph.render.sw-span_buffer.cppm render/src/aleph.render.sw/aleph.render.sw-rast_scan.cppm render/src/aleph.render.sw/aleph.render.sw.cppm render/src/aleph.render.sw/CMakeLists.txt tests/render/test_sw_span_buffer.cpp tests/render/test_sw_rasterize.cpp tests/CMakeLists.txt
git commit -m "task 15: aleph.render.sw:span_buffer + :rast_scan — scanline subspan FDIV w/ lightmap modulation"
```

---

## Task 16: `aleph.render.sw:lightmap` — bilinear sample + bake

**Files:**
- Create: `render/src/aleph.render.sw/aleph.render.sw-lightmap.cppm`
- Create: `tests/render/test_sw_lightmap.cpp`

- [ ] **Step 16.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.render.sw;
import aleph.math;

using namespace aleph::render::sw;

TEST_CASE("lightmap_sample_bilinear: solid white lightmap returns white") {
    aleph::math::u32 texels[16];
    for (auto& t : texels) t = 0xFFFFFFFFu;
    Lightmap lm{texels, 4, 4, 0.0f, 1.0f, 0.0f, 1.0f};
    const aleph::math::u32 s = lightmap_sample_bilinear(lm, 0.5f, 0.5f);
    CHECK(((s >> 16) & 0xFFu) == 0xFFu);
    CHECK(((s >>  8) & 0xFFu) == 0xFFu);
    CHECK((s        & 0xFFu) == 0xFFu);
}

TEST_CASE("lightmap_bake: face + one light → texels in expected range") {
    SceneRT sr;
    Face floor{};
    floor.verts = {aleph::math::Vec3{-1, 0, -1}, aleph::math::Vec3{1, 0, -1},
                   aleph::math::Vec3{1, 0, 1},  aleph::math::Vec3{-1, 0, 1}};
    floor.uvs   = {aleph::math::Vec2{0,0}, aleph::math::Vec2{1,0},
                   aleph::math::Vec2{1,1}, aleph::math::Vec2{0,1}};
    floor.tex   = tex_floor;
    floor.lightmap_id = 0;
    sr.faces.push_back(floor);
    sr.lightmaps.push_back(Lightmap{});

    const int LM = 8;
    sr.lightmap_pool.resize(LM * LM, 0u);
    sr.lightmaps[0].texels = sr.lightmap_pool.data();
    sr.lightmaps[0].w = LM;
    sr.lightmaps[0].h = LM;

    bake_lightmaps(sr, aleph::math::Vec3{0, 5, 0}, 10.0f, 0.05f);
    // Every texel should be > 0 (ambient at least)
    bool any_lit = false;
    for (auto t : sr.lightmap_pool)
        if ((t & 0xFFu) > 0u) { any_lit = true; break; }
    CHECK(any_lit);
}
```

- [ ] **Step 16.2: Write `aleph.render.sw-lightmap.cppm`**

Port of Sotark's `sotark.sw-lightmap.cppm`, adapted to the new `Face` + `SceneRT`
and `lightmap_pool` storage layout:

```cpp
module;
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <span>

export module aleph.render.sw:lightmap;

import aleph.math;
import :scene_rt;

export namespace aleph::render::sw {

[[nodiscard]] inline aleph::math::u32
lightmap_sample_bilinear(const Lightmap& lm, aleph::math::f32 u, aleph::math::f32 v) noexcept {
    aleph::math::f32 fu = (u - lm.u_min) / (lm.u_max - lm.u_min)
                          * static_cast<aleph::math::f32>(lm.w - 1);
    aleph::math::f32 fv = (v - lm.v_min) / (lm.v_max - lm.v_min)
                          * static_cast<aleph::math::f32>(lm.h - 1);
    fu = std::max(0.0f, fu);
    fv = std::max(0.0f, fv);
    const aleph::math::f32 max_u = static_cast<aleph::math::f32>(lm.w - 1) - 1e-4f;
    const aleph::math::f32 max_v = static_cast<aleph::math::f32>(lm.h - 1) - 1e-4f;
    if (fu > max_u) fu = max_u;
    if (fv > max_v) fv = max_v;
    const int iu = static_cast<int>(std::floor(fu));
    const int iv = static_cast<int>(std::floor(fv));
    const aleph::math::f32 fx = fu - static_cast<aleph::math::f32>(iu);
    const aleph::math::f32 fy = fv - static_cast<aleph::math::f32>(iv);
    const aleph::math::u32 c00 = lm.texels[iv      * lm.w + iu    ];
    const aleph::math::u32 c10 = lm.texels[iv      * lm.w + iu + 1];
    const aleph::math::u32 c01 = lm.texels[(iv+1)  * lm.w + iu    ];
    const aleph::math::u32 c11 = lm.texels[(iv+1)  * lm.w + iu + 1];
    const aleph::math::f32 w00 = (1.0f - fx) * (1.0f - fy);
    const aleph::math::f32 w10 = fx          * (1.0f - fy);
    const aleph::math::f32 w01 = (1.0f - fx) * fy;
    const aleph::math::f32 w11 = fx          * fy;
    auto mix = [&](aleph::math::u32 shift) {
        return static_cast<aleph::math::u32>(
            static_cast<aleph::math::f32>((c00 >> shift) & 0xFFu) * w00 +
            static_cast<aleph::math::f32>((c10 >> shift) & 0xFFu) * w10 +
            static_cast<aleph::math::f32>((c01 >> shift) & 0xFFu) * w01 +
            static_cast<aleph::math::f32>((c11 >> shift) & 0xFFu) * w11);
    };
    return 0xFF000000u | (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

namespace detail {

inline aleph::math::Vec3 quad_interp(const std::array<aleph::math::Vec3, 4>& verts,
                                      aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::Vec3 bottom = verts[0] * (1.0f - u) + verts[1] * u;
    const aleph::math::Vec3 top    = verts[3] * (1.0f - u) + verts[2] * u;
    return bottom * (1.0f - v) + top * v;
}

inline bool quad_blocks(const Face& face, aleph::math::Vec3 origin,
                         aleph::math::Vec3 dir, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 n  = aleph::math::cross(e1, e2);
    const aleph::math::f32 denom = aleph::math::dot(n, dir);
    if (std::abs(denom) < 1e-8f) return false;
    const aleph::math::f32 D = aleph::math::dot(n, face.verts[0]);
    const aleph::math::f32 t = (D - aleph::math::dot(n, origin)) / denom;
    if (t <= 0.001f || t >= t_max) return false;
    const aleph::math::Vec3 P = origin + dir * t;
    const aleph::math::Vec3 local = P - face.verts[0];
    const aleph::math::f32 a = aleph::math::dot(local, e1) / aleph::math::dot(e1, e1);
    const aleph::math::f32 b = aleph::math::dot(local, e2) / aleph::math::dot(e2, e2);
    return (a >= 0.0f && a <= 1.0f && b >= 0.0f && b <= 1.0f);
}

}  // namespace detail

inline void bake_lightmap_face(Lightmap& lm,
                                 const Face& face,
                                 std::span<const Face> all_faces,
                                 int skip_idx,
                                 aleph::math::Vec3 light_pos,
                                 aleph::math::f32 intensity,
                                 aleph::math::f32 ambient) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 normal = aleph::math::normalize(aleph::math::cross(e1, e2));
    aleph::math::f32 u_min = face.uvs[0].x, u_max = face.uvs[0].x;
    aleph::math::f32 v_min = face.uvs[0].y, v_max = face.uvs[0].y;
    for (int i = 1; i < 4; ++i) {
        u_min = std::min(u_min, face.uvs[i].x);
        u_max = std::max(u_max, face.uvs[i].x);
        v_min = std::min(v_min, face.uvs[i].y);
        v_max = std::max(v_max, face.uvs[i].y);
    }
    lm.u_min = u_min; lm.u_max = u_max; lm.v_min = v_min; lm.v_max = v_max;
    const int n_faces = static_cast<int>(all_faces.size());
    for (int j = 0; j < lm.h; ++j) {
        for (int i = 0; i < lm.w; ++i) {
            const aleph::math::f32 pu = (static_cast<aleph::math::f32>(i) + 0.5f)
                                         / static_cast<aleph::math::f32>(lm.w);
            const aleph::math::f32 pv = (static_cast<aleph::math::f32>(j) + 0.5f)
                                         / static_cast<aleph::math::f32>(lm.h);
            const aleph::math::Vec3 P = detail::quad_interp(face.verts, pu, pv);
            const aleph::math::Vec3 P_off = P + normal * 0.001f;
            const aleph::math::Vec3 to_L = light_pos - P_off;
            const aleph::math::f32 dist_sq = aleph::math::length_sq(to_L);
            const aleph::math::f32 dist = std::sqrt(dist_sq);
            const aleph::math::Vec3 L_dir = to_L * (1.0f / dist);
            const aleph::math::f32 cos_theta = aleph::math::dot(normal, L_dir);
            aleph::math::f32 lit = ambient;
            if (cos_theta > 0.0f) {
                bool blocked = false;
                for (int k = 0; k < n_faces; ++k) {
                    if (k == skip_idx) continue;
                    if (detail::quad_blocks(all_faces[k], P_off, to_L, 1.0f)) {
                        blocked = true; break;
                    }
                }
                if (!blocked) lit += intensity * cos_theta / dist_sq;
            }
            lit = std::clamp(lit, 0.0f, 1.0f);
            const aleph::math::u8 b = static_cast<aleph::math::u8>(lit * 255.0f);
            lm.texels[j * lm.w + i] = 0xFF000000u
                                      | (static_cast<aleph::math::u32>(b) << 16)
                                      | (static_cast<aleph::math::u32>(b) <<  8)
                                      |  static_cast<aleph::math::u32>(b);
        }
    }
}

inline void bake_lightmaps(SceneRT& sr,
                            aleph::math::Vec3 light_pos,
                            aleph::math::f32 intensity,
                            aleph::math::f32 ambient) noexcept {
    const int n = static_cast<int>(sr.faces.size());
    for (int i = 0; i < n; ++i) {
        const aleph::math::u32 lmid = sr.faces[i].lightmap_id;
        if (lmid == 0xFFFFFFFFu) continue;
        bake_lightmap_face(sr.lightmaps[lmid], sr.faces[i],
                            std::span<const Face>{sr.faces}, i,
                            light_pos, intensity, ambient);
    }
}

}  // namespace aleph::render::sw
```

- [ ] **Step 16.3: Wire + build + test + commit**

`aleph.render.sw.cppm` adds `export import :lightmap;`. CMake FILE_SET grows.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.sw/aleph.render.sw-lightmap.cppm render/src/aleph.render.sw/aleph.render.sw.cppm render/src/aleph.render.sw/CMakeLists.txt tests/render/test_sw_lightmap.cpp tests/CMakeLists.txt
git commit -m "task 16: aleph.render.sw:lightmap — bilinear sample + per-face shadow bake"
```

---

## Task 17: `aleph.render.sw:rasterize` — full-scene driver + primitive builders

**Files:**
- Create: `render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm`
- Create: `render/src/aleph.render.sw/aleph.render.sw-primitives.cppm`
- Create: `tests/render/test_sw_rasterize_full.cpp`

- [ ] **Step 17.1: Write failing test**

```cpp
#include "doctest.h"
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::sw;

TEST_CASE("rasterize: SceneRT with one floor → film has non-magenta pixels") {
    SceneRT sr;
    add_floor(sr, aleph::math::Vec3{0, 0, 0}, 4.0f, tex_floor);
    sr.lightmap_pool.resize(32 * 32, 0xFFFFFFFFu);   // all white = no shadows
    sr.lightmaps[0].texels = sr.lightmap_pool.data();
    sr.lightmaps[0].w = 32; sr.lightmaps[0].h = 32;
    sr.lightmaps[0].u_min = -2.0f; sr.lightmaps[0].u_max = 2.0f;
    sr.lightmaps[0].v_min = -2.0f; sr.lightmaps[0].v_max = 2.0f;

    alignas(64) static unsigned char fbuf[32 * 32 * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(arena, 32, 32);
    for (int i = 0; i < 32 * 32; ++i) film.pixels[i] = aleph::math::Vec3{1, 0, 1};
    std::vector<aleph::math::f32> depth(32 * 32, 1.0f);

    const aleph::math::Mat4 view = aleph::math::Mat4::look_at(
        aleph::math::Vec3{0, 2, 5}, aleph::math::Vec3{0, 0, 0}, aleph::math::Vec3{0, 1, 0});
    const aleph::math::Mat4 proj = aleph::math::Mat4::perspective(
        aleph::math::deg_to_rad(60.0f), 1.0f, 0.05f, 100.0f);
    const aleph::math::Mat4 mvp = proj * view;

    aleph::threads::Pool pool(2);
    rasterize(sr, mvp, film, depth, pool);

    int touched = 0;
    for (int i = 0; i < 32 * 32; ++i)
        if (!(film.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    CHECK(touched > 0);
}
```

- [ ] **Step 17.2: Write `aleph.render.sw-primitives.cppm`**

```cpp
module;
#include <cstdint>
#include <array>

export module aleph.render.sw:primitives;

import aleph.math;
import :scene_rt;

export namespace aleph::render::sw {

// Append a single-face floor. Returns face index.
inline std::uint32_t add_floor(SceneRT& s, aleph::math::Vec3 c,
                                aleph::math::f32 size, TexSampleFn tex) noexcept {
    const aleph::math::f32 h = size * 0.5f;
    Face f{};
    f.verts = {
        aleph::math::Vec3{c.x - h, c.y, c.z + h},
        aleph::math::Vec3{c.x + h, c.y, c.z + h},
        aleph::math::Vec3{c.x + h, c.y, c.z - h},
        aleph::math::Vec3{c.x - h, c.y, c.z - h},
    };
    f.uvs = {
        aleph::math::Vec2{-h,  h}, aleph::math::Vec2{ h,  h},
        aleph::math::Vec2{ h, -h}, aleph::math::Vec2{-h, -h},
    };
    f.tex = tex;
    f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
    s.faces.push_back(f);
    s.lightmaps.push_back(Lightmap{});
    return static_cast<std::uint32_t>(s.faces.size() - 1);
}

// 6-face cube centered at c with side size, all faces use `tex`.
inline std::uint32_t add_cube(SceneRT& s, aleph::math::Vec3 c,
                               aleph::math::f32 size, TexSampleFn tex) noexcept {
    const aleph::math::f32 h = size * 0.5f;
    const aleph::math::Vec3 mn{c.x - h, c.y - h, c.z - h};
    const aleph::math::Vec3 mx{c.x + h, c.y + h, c.z + h};
    const std::uint32_t first = static_cast<std::uint32_t>(s.faces.size());
    auto add_face = [&](std::array<aleph::math::Vec3, 4> verts,
                          std::array<aleph::math::Vec2, 4> uvs) {
        Face f{};
        f.verts = verts;
        f.uvs   = uvs;
        f.tex   = tex;
        f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
        s.faces.push_back(f);
        s.lightmaps.push_back(Lightmap{});
    };
    // Top
    add_face({aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mx.x, mx.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.x, mx.z}, aleph::math::Vec2{mx.x, mx.z},
              aleph::math::Vec2{mx.x, mn.z}, aleph::math::Vec2{mn.x, mn.z}});
    // Bottom
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mn.x, mn.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.z}, aleph::math::Vec2{mx.x, mn.z},
              aleph::math::Vec2{mx.x, mx.z}, aleph::math::Vec2{mn.x, mx.z}});
    // North (+z)
    add_face({aleph::math::Vec3{mn.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.y}, aleph::math::Vec2{mx.x, mn.y},
              aleph::math::Vec2{mx.x, mx.y}, aleph::math::Vec2{mn.x, mx.y}});
    // South (-z)
    add_face({aleph::math::Vec3{mx.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mn.z},
              aleph::math::Vec3{mn.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mn.z}},
             {aleph::math::Vec2{mx.x, mn.y}, aleph::math::Vec2{mn.x, mn.y},
              aleph::math::Vec2{mn.x, mx.y}, aleph::math::Vec2{mx.x, mx.y}});
    // East (+x)
    add_face({aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mx.z}},
             {aleph::math::Vec2{mx.z, mn.y}, aleph::math::Vec2{mn.z, mn.y},
              aleph::math::Vec2{mn.z, mx.y}, aleph::math::Vec2{mx.z, mx.y}});
    // West (-x)
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mx.z},
              aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.z, mn.y}, aleph::math::Vec2{mx.z, mn.y},
              aleph::math::Vec2{mx.z, mx.y}, aleph::math::Vec2{mn.z, mx.y}});
    return first;
}

// Pillar (4 walls + top, no bottom). Same builder pattern as cube but parametric height.
inline std::uint32_t add_pillar(SceneRT& s, aleph::math::Vec3 base_c,
                                  aleph::math::f32 width, aleph::math::f32 height,
                                  TexSampleFn tex) noexcept {
    const aleph::math::f32 hw = width * 0.5f;
    const aleph::math::Vec3 mn{base_c.x - hw, base_c.y,          base_c.z - hw};
    const aleph::math::Vec3 mx{base_c.x + hw, base_c.y + height, base_c.z + hw};
    const std::uint32_t first = static_cast<std::uint32_t>(s.faces.size());
    auto add_face = [&](std::array<aleph::math::Vec3, 4> verts,
                          std::array<aleph::math::Vec2, 4> uvs) {
        Face f{};
        f.verts = verts; f.uvs = uvs; f.tex = tex;
        f.lightmap_id = static_cast<std::uint32_t>(s.lightmaps.size());
        s.faces.push_back(f);
        s.lightmaps.push_back(Lightmap{});
    };
    add_face({aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mx.x, mx.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.x, mx.z}, aleph::math::Vec2{mx.x, mx.z},
              aleph::math::Vec2{mx.x, mn.z}, aleph::math::Vec2{mn.x, mn.z}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mx.z},
              aleph::math::Vec3{mx.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mx.z}},
             {aleph::math::Vec2{mn.x, mn.y}, aleph::math::Vec2{mx.x, mn.y},
              aleph::math::Vec2{mx.x, mx.y}, aleph::math::Vec2{mn.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mn.z},
              aleph::math::Vec3{mn.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mn.z}},
             {aleph::math::Vec2{mx.x, mn.y}, aleph::math::Vec2{mn.x, mn.y},
              aleph::math::Vec2{mn.x, mx.y}, aleph::math::Vec2{mx.x, mx.y}});
    add_face({aleph::math::Vec3{mx.x, mn.y, mx.z}, aleph::math::Vec3{mx.x, mn.y, mn.z},
              aleph::math::Vec3{mx.x, mx.y, mn.z}, aleph::math::Vec3{mx.x, mx.y, mx.z}},
             {aleph::math::Vec2{mx.z, mn.y}, aleph::math::Vec2{mn.z, mn.y},
              aleph::math::Vec2{mn.z, mx.y}, aleph::math::Vec2{mx.z, mx.y}});
    add_face({aleph::math::Vec3{mn.x, mn.y, mn.z}, aleph::math::Vec3{mn.x, mn.y, mx.z},
              aleph::math::Vec3{mn.x, mx.y, mx.z}, aleph::math::Vec3{mn.x, mx.y, mn.z}},
             {aleph::math::Vec2{mn.z, mn.y}, aleph::math::Vec2{mx.z, mn.y},
              aleph::math::Vec2{mx.z, mx.y}, aleph::math::Vec2{mn.z, mx.y}});
    return first;
}

}  // namespace aleph::render::sw
```

- [ ] **Step 17.3: Write `aleph.render.sw-rasterize.cppm`**

```cpp
module;
#include <vector>
#include <span>
#include <array>
#include <cstdint>
#include <algorithm>

export module aleph.render.sw:rasterize;

import aleph.math;
import aleph.render.common;
import aleph.threads;
import :scene_rt;
import :clip;
import :span_buffer;
import :rast_scan;

export namespace aleph::render::sw {

namespace detail {

inline aleph::math::Vec3 face_center(const Face& f) noexcept {
    return aleph::math::Vec3{
        (f.verts[0].x + f.verts[1].x + f.verts[2].x + f.verts[3].x) * 0.25f,
        (f.verts[0].y + f.verts[1].y + f.verts[2].y + f.verts[3].y) * 0.25f,
        (f.verts[0].z + f.verts[1].z + f.verts[2].z + f.verts[3].z) * 0.25f,
    };
}

inline ScreenVert to_screen(ClipVert cv, int W, int H) noexcept {
    const aleph::math::f32 invw = 1.0f / cv.w;
    return ScreenVert{
        (cv.x * invw * 0.5f + 0.5f) * static_cast<aleph::math::f32>(W),
        (1.0f - (cv.y * invw * 0.5f + 0.5f)) * static_cast<aleph::math::f32>(H),
        cv.z * invw,
        cv.u, cv.v,
        invw,
    };
}

}  // namespace detail

inline void rasterize(const SceneRT& sr, aleph::math::Mat4 mvp,
                       aleph::render::common::Film& fb,
                       std::span<aleph::math::f32> depth,
                       aleph::threads::Pool& pool) noexcept {
    const int N = static_cast<int>(sr.faces.size());
    if (N == 0) return;

    // Front-to-back sort by face centroid distance from camera (camera position
    // recoverable from mvp's inverse — for simplicity here, we sort by clip-space z).
    std::vector<int> order(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) order[i] = i;
    std::vector<aleph::math::f32> dist_sq(static_cast<std::size_t>(N), 0.0f);
    // Project face centers to clip space, use z/w as depth proxy
    for (int i = 0; i < N; ++i) {
        const aleph::math::Vec3 c = detail::face_center(sr.faces[i]);
        const aleph::math::Vec4 cp = mvp * aleph::math::Vec4{c.x, c.y, c.z, 1.0f};
        dist_sq[i] = cp.z / (cp.w > 1e-6f ? cp.w : 1e-6f);
    }
    std::sort(order.begin(), order.end(),
              [&](int a, int b){ return dist_sq[a] < dist_sq[b]; });

    // Project all face verts to clip space.
    std::vector<std::array<ClipVert, 4>> clip_verts(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < 4; ++k) {
            const auto& vt = sr.faces[i].verts[k];
            const auto& uv = sr.faces[i].uvs[k];
            const aleph::math::Vec4 v{vt.x, vt.y, vt.z, 1.0f};
            const aleph::math::Vec4 cp = mvp * v;
            clip_verts[i][k] = ClipVert{cp.x, cp.y, cp.z, cp.w, uv.x, uv.y};
        }
    }

    // Each worker draws a horizontal stripe. Per-thread SpanBuffer.
    const int n_threads = std::max(1, pool.n_threads());
    pool.parallel_for(0, n_threads, [&](int worker_id) {
        SpanBuffer sb(fb.width, fb.height);
        const int y_start = (fb.height * worker_id)       / n_threads;
        const int y_end   = (fb.height * (worker_id + 1)) / n_threads;
        constexpr std::array<std::array<aleph::math::u8, 3>, 2> quad_tris{{
            {0, 1, 2}, {0, 2, 3}
        }};
        for (int oi = 0; oi < N; ++oi) {
            const int idx = order[oi];
            const Face& face = sr.faces[idx];
            const auto& cv = clip_verts[idx];
            const Lightmap* lm = (face.lightmap_id == 0xFFFFFFFFu)
                                  ? nullptr : &sr.lightmaps[face.lightmap_id];
            for (int t = 0; t < 2; ++t) {
                const ClipVert a = cv[quad_tris[t][0]];
                const ClipVert b = cv[quad_tris[t][1]];
                const ClipVert c = cv[quad_tris[t][2]];
                std::array<ClipVert, 6> clipped{};
                const int n_tris = clip_triangle_near(a, b, c, 0.1f, clipped);
                for (int k = 0; k < n_tris; ++k) {
                    const ScreenVert s0 = detail::to_screen(clipped[k*3 + 0], fb.width, fb.height);
                    const ScreenVert s1 = detail::to_screen(clipped[k*3 + 1], fb.width, fb.height);
                    const ScreenVert s2 = detail::to_screen(clipped[k*3 + 2], fb.width, fb.height);
                    rast_scan_textured(fb, depth, sb, s0, s1, s2, face.tex, lm,
                                        y_start, y_end);
                }
            }
        }
    });
}

}  // namespace aleph::render::sw
```

- [ ] **Step 17.4: Wire + build + test + commit**

`aleph.render.sw.cppm` adds `export import :primitives; export import :rasterize;`. CMake FILE_SET grows.

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.render.sw/aleph.render.sw-primitives.cppm render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm render/src/aleph.render.sw/aleph.render.sw.cppm render/src/aleph.render.sw/CMakeLists.txt tests/render/test_sw_rasterize_full.cpp tests/CMakeLists.txt
git commit -m "task 17: aleph.render.sw:primitives + :rasterize — full-scene driver + cube/floor/pillar builders; sw module complete"
```

---

## Task 18: `aleph.window` — SDL2 wrapper

**Files:**
- Create: `render/src/aleph.window/CMakeLists.txt`
- Create: `render/src/aleph.window/aleph.window.cppm`
- Create: `render/src/aleph.window/aleph.window-event.cppm`
- Create: `render/src/aleph.window/aleph.window-window.cppm`
- Create: `tests/window/test_window_init.cpp`
- Modify: `render/CMakeLists.txt` — wrap in `if(ALEPH_HAVE_SDL2) add_subdirectory(...) endif()`

- [ ] **Step 18.1: Write test (conditional)**

```cpp
#include "doctest.h"
#include <cstdlib>
import aleph.window;

using namespace aleph::window;

TEST_CASE("Window creates + destroys without crash") {
    // Skip if no display (CI). Detect via SDL_VIDEODRIVER=dummy env or DISPLAY absent.
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        WARN("No display — skipping window init test.");
        return;
    }
    Window w(160, 120, "aleph_test_window");
    CHECK(w.width()  == 160);
    CHECK(w.height() == 120);
    CHECK(w.pixels() != nullptr);
}
```

`mkdir -p tests/window` if needed.

- [ ] **Step 18.2: Write `aleph.window-event.cppm`**

```cpp
export module aleph.window:event;

export namespace aleph::window {

struct Event {
    enum class Kind { Quit, KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseWheel };
    Kind kind;
    int  key{0};
    int  button{0};
    int  x{0}, y{0};
    int  dx{0}, dy{0};
    int  wheel{0};
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
};

}  // namespace aleph::window
```

- [ ] **Step 18.3: Write `aleph.window-window.cppm`**

```cpp
module;
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <SDL2/SDL.h>

export module aleph.window:window;

import aleph.math;
import :event;

export namespace aleph::window {

class Window {
public:
    Window(int w, int h, const char* title) noexcept {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "aleph.window: SDL_Init failed: %s\n", SDL_GetError());
            std::abort();
        }
        win_ = SDL_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
        if (!win_) {
            std::fprintf(stderr, "aleph.window: SDL_CreateWindow: %s\n", SDL_GetError());
            std::abort();
        }
        back_ = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!back_) {
            std::fprintf(stderr, "aleph.window: SDL_CreateRGBSurfaceWithFormat: %s\n",
                          SDL_GetError());
            std::abort();
        }
        w_ = w; h_ = h;
    }

    ~Window() {
        if (back_) SDL_FreeSurface(back_);
        if (win_)  SDL_DestroyWindow(win_);
        SDL_Quit();
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    int poll_events(std::span<Event> out) noexcept {
        int n = 0;
        SDL_Event ev;
        while (n < static_cast<int>(out.size()) && SDL_PollEvent(&ev)) {
            Event& e = out[n];
            switch (ev.type) {
                case SDL_QUIT:           e.kind = Event::Kind::Quit; ++n; break;
                case SDL_KEYDOWN:
                    e.kind = Event::Kind::KeyDown;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_KEYUP:
                    e.kind = Event::Kind::KeyUp;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_MOUSEBUTTONDOWN:
                    e.kind = Event::Kind::MouseDown;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEBUTTONUP:
                    e.kind = Event::Kind::MouseUp;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEMOTION:
                    e.kind = Event::Kind::MouseMove;
                    e.x  = ev.motion.x;     e.y  = ev.motion.y;
                    e.dx = ev.motion.xrel;  e.dy = ev.motion.yrel;
                    ++n; break;
                case SDL_MOUSEWHEEL:
                    e.kind = Event::Kind::MouseWheel;
                    e.wheel = ev.wheel.y;
                    ++n; break;
                default: break;
            }
        }
        return n;
    }

    void present() noexcept {
        SDL_Surface* ws = SDL_GetWindowSurface(win_);
        if (ws) {
            SDL_BlitSurface(back_, nullptr, ws, nullptr);
            SDL_UpdateWindowSurface(win_);
        }
    }

    aleph::math::u32* pixels() noexcept {
        return static_cast<aleph::math::u32*>(back_->pixels);
    }
    int pitch_pixels() const noexcept { return back_->pitch / 4; }
    int width()  const noexcept { return w_; }
    int height() const noexcept { return h_; }

    aleph::math::u32 ticks_ms() const noexcept { return SDL_GetTicks(); }
    aleph::math::u64 perf_counter()   const noexcept { return SDL_GetPerformanceCounter(); }
    aleph::math::u64 perf_frequency() const noexcept { return SDL_GetPerformanceFrequency(); }

private:
    SDL_Window*  win_{nullptr};
    SDL_Surface* back_{nullptr};
    int w_{0}, h_{0};
};

}  // namespace aleph::window
```

- [ ] **Step 18.4: Primary + CMake**

`aleph.window.cppm`:
```cpp
export module aleph.window;
export import :event;
export import :window;
```

`render/src/aleph.window/CMakeLists.txt`:
```cmake
add_library(aleph_window)
target_sources(aleph_window
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.window.cppm
        aleph.window-event.cppm
        aleph.window-window.cppm)
target_link_libraries(aleph_window
    PUBLIC  aleph_math
    PRIVATE aleph_flags_isa)
if(TARGET SDL2::SDL2)
    target_link_libraries(aleph_window PUBLIC SDL2::SDL2)
elseif(TARGET PkgConfig::SDL2)
    target_link_libraries(aleph_window PUBLIC PkgConfig::SDL2)
else()
    target_include_directories(aleph_window PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(aleph_window PUBLIC ${SDL2_LIBRARIES})
endif()
```

In `render/CMakeLists.txt`, replace the commented-out window line with:
```cmake
if(ALEPH_HAVE_SDL2)
    add_subdirectory(src/aleph.window)
endif()
```

In `tests/CMakeLists.txt`, conditionally add the window test:
```cmake
if(ALEPH_HAVE_SDL2)
    target_sources(aleph_tests PRIVATE window/test_window_init.cpp)
    target_link_libraries(aleph_tests PRIVATE aleph_window)
endif()
```

- [ ] **Step 18.5: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.window/ render/CMakeLists.txt tests/window/test_window_init.cpp tests/CMakeLists.txt
git commit -m "task 18: aleph.window — SDL2 wrapper (Window + Event, only file linking SDL2)"
```

---

## Task 19: `aleph.editor` — bitmap font + UI immediate-mode + orbit cam + picking

**Files:**
- Create: `render/src/aleph.editor/CMakeLists.txt`
- Create: `render/src/aleph.editor/aleph.editor.cppm`
- Create: `render/src/aleph.editor/aleph.editor-font.cppm`
- Create: `render/src/aleph.editor/aleph.editor-ui.cppm`
- Create: `render/src/aleph.editor/aleph.editor-orbit.cppm`
- Create: `render/src/aleph.editor/aleph.editor-picking.cppm`
- Create: `tests/editor/test_orbit.cpp`
- Create: `tests/editor/test_pick.cpp`

- [ ] **Step 19.1: Write failing tests**

`tests/editor/test_orbit.cpp`:
```cpp
#include "doctest.h"
import aleph.editor;
import aleph.window;
import aleph.math;

using namespace aleph::editor;

TEST_CASE("orbit_eye: target=origin, radius=5, yaw=0, pitch=0 → eye at (0,0,5)") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 5.0f};
    aleph::math::Vec3 e = orbit_eye(c);
    CHECK(e.z == doctest::Approx(5.0f));
    CHECK(e.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(e.y == doctest::Approx(0.0f).epsilon(1e-5f));
}

TEST_CASE("orbit_handle: MouseMove with no buttons → no change") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.3f, 0.25f, 8.0f};
    aleph::window::Event e{};
    e.kind = aleph::window::Event::Kind::MouseMove;
    e.dx = 10; e.dy = 5;
    const auto before = c;
    orbit_handle(c, e, /*left_down=*/false, /*right_down=*/false);
    CHECK(c.yaw == before.yaw);
    CHECK(c.pitch == before.pitch);
}

TEST_CASE("orbit_handle: MouseWheel zooms radius") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 8.0f};
    aleph::window::Event e{};
    e.kind = aleph::window::Event::Kind::MouseWheel;
    e.wheel = 1;  // zoom in
    orbit_handle(c, e, false, false);
    CHECK(c.radius < 8.0f);
}
```

`tests/editor/test_pick.cpp`:
```cpp
#include "doctest.h"
import aleph.editor;
import aleph.render.sw;
import aleph.math;

using namespace aleph::editor;

TEST_CASE("pick_face: ray hits a known face → returns its index") {
    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, aleph::math::Vec3{0, 0, 0}, 2.0f,
                                  aleph::render::sw::tex_floor);
    // Pick the center of an 800x600 viewport with camera looking at origin.
    const int i = pick_face(sr, 400, 300,
                              aleph::math::Vec3{0, 5, 0},   // eye above
                              aleph::math::Vec3{0, 0, 0},   // target
                              aleph::math::Vec3{0, 0, -1},  // up = forward in horizontal plane
                              aleph::math::deg_to_rad(60.0f), 4.0f/3.0f, 800, 600);
    CHECK(i == 0);
}
```

`mkdir -p tests/editor`.

- [ ] **Step 19.2: Write `aleph.editor-font.cppm`**

Port Sotark cxx26's `sotark.sw-text.cppm` directly. Same 8x8 bitmap font data table.

```cpp
module;
#include <array>
#include <string_view>
#include <algorithm>
#include <cstdint>

export module aleph.editor:font;

import aleph.math;
import aleph.render.common;

export namespace aleph::editor {

namespace detail {
inline constexpr std::array<std::array<aleph::math::u8, 8>, 128> font = []() {
    std::array<std::array<aleph::math::u8, 8>, 128> f{};
    f[' '] = {0,0,0,0,0,0,0,0};
    f['.'] = {0,0,0,0,0,0,0x18,0x18};
    f[','] = {0,0,0,0,0,0x18,0x18,0x30};
    f[':'] = {0,0x18,0x18,0,0,0x18,0x18,0};
    f['-'] = {0,0,0,0x7E,0,0,0,0};
    f['+'] = {0,0x18,0x18,0x7E,0x18,0x18,0,0};
    f['('] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0};
    f[')'] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0};
    f['/'] = {0,0x06,0x0C,0x18,0x30,0x60,0,0};
    f['='] = {0,0,0x7E,0,0x7E,0,0,0};
    f['%'] = {0x63,0x66,0x0C,0x18,0x30,0x66,0x63,0};
    f['0'] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0};
    f['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0};
    f['2'] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0};
    f['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0};
    f['4'] = {0x06,0x0E,0x1E,0x66,0x7F,0x06,0x06,0};
    f['5'] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0};
    f['6'] = {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0};
    f['7'] = {0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0};
    f['8'] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0};
    f['9'] = {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0};
    // Uppercase A..Z (full Sotark table) — paste from Sotark sw-text.cppm verbatim.
    f['A'] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0};
    f['B'] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0};
    f['C'] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0};
    f['D'] = {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0};
    f['E'] = {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0};
    f['F'] = {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0};
    f['G'] = {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0};
    f['H'] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0};
    f['I'] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0};
    f['J'] = {0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0};
    f['K'] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0};
    f['L'] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0};
    f['M'] = {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0};
    f['N'] = {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0};
    f['O'] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0};
    f['P'] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0};
    f['Q'] = {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x06,0};
    f['R'] = {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0};
    f['S'] = {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0};
    f['T'] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0};
    f['U'] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0};
    f['V'] = {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0};
    f['W'] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0};
    f['X'] = {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0};
    f['Y'] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0};
    f['Z'] = {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0};
    return f;
}();
}  // namespace detail

// Plot a single pixel into the Film, bounds-checked.
inline void plot(aleph::render::common::Film& fb, int x, int y,
                  aleph::math::Vec3 color) noexcept {
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(fb.width)) return;
    if (static_cast<unsigned>(y) >= static_cast<unsigned>(fb.height)) return;
    fb.pixels[y * fb.stride_pixels + x] = color;
}

inline void draw_text(aleph::render::common::Film& fb, int x, int y,
                       std::string_view s, aleph::math::Vec3 color) noexcept {
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const auto& glyph = detail::font[c < 128 ? c : 0];
        for (int gy = 0; gy < 8; ++gy) {
            const aleph::math::u8 row = glyph[gy];
            for (int gx = 0; gx < 8; ++gx) {
                if (row & (0x80u >> gx)) plot(fb, x + gx, y + gy, color);
            }
        }
        x += 8;
    }
}

inline void draw_text_shadowed(aleph::render::common::Film& fb, int x, int y,
                                std::string_view s, aleph::math::Vec3 fg) noexcept {
    draw_text(fb, x + 1, y + 1, s, aleph::math::Vec3{0, 0, 0});
    draw_text(fb, x,     y,     s, fg);
}

inline void draw_rect(aleph::render::common::Film& fb, int x, int y, int w, int h,
                       aleph::math::Vec3 color) noexcept {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(fb.width,  x + w);
    const int y1 = std::min(fb.height, y + h);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            fb.pixels[yy * fb.stride_pixels + xx] = color;
}

}  // namespace aleph::editor
```

- [ ] **Step 19.3: Write `aleph.editor-ui.cppm`**

Port of Sotark cxx26's `sotark.sw-ui.cppm`, adapted to `Film` (Vec3) instead of `framebuf`:

```cpp
module;
#include <cstdint>
#include <string_view>
#include <algorithm>

export module aleph.editor:ui;

import aleph.math;
import aleph.render.common;
import :font;

export namespace aleph::editor {

inline aleph::math::Vec3 ui_color(aleph::math::u8 r, aleph::math::u8 g, aleph::math::u8 b) noexcept {
    return aleph::math::Vec3{
        static_cast<aleph::math::f32>(r) / 255.0f,
        static_cast<aleph::math::f32>(g) / 255.0f,
        static_cast<aleph::math::f32>(b) / 255.0f,
    };
}

struct UiCtx {
    int  mouse_x{0}, mouse_y{0};
    bool mouse_down{false};
    bool mouse_pressed{false};
    int  hot_id{0};
    int  active_id{0};
    aleph::render::common::Film* fb{nullptr};
};

inline bool in_rect(const UiCtx& u, int x, int y, int w, int h) noexcept {
    return u.mouse_x >= x && u.mouse_x < x + w
        && u.mouse_y >= y && u.mouse_y < y + h;
}

inline void ui_begin(UiCtx& u, aleph::render::common::Film* fb, int mx, int my,
                      bool md, bool mp) noexcept {
    u.mouse_x = mx; u.mouse_y = my;
    u.mouse_down = md; u.mouse_pressed = mp;
    u.hot_id = 0; u.fb = fb;
}

inline void ui_end(UiCtx& u) noexcept {
    if (!u.mouse_down) u.active_id = 0;
}

inline void ui_panel(UiCtx& u, int x, int y, int w, int h, std::string_view title) {
    draw_rect(*u.fb, x, y, w, h, ui_color(28, 32, 44));
    draw_rect(*u.fb, x, y, w, 20, ui_color(50, 60, 86));
    if (!title.empty()) draw_text_shadowed(*u.fb, x + 6, y + 6, title, ui_color(230, 230, 230));
    draw_rect(*u.fb, x, y, w, 1, ui_color(80, 90, 120));
    draw_rect(*u.fb, x, y + h - 1, w, 1, ui_color(15, 18, 25));
}

inline void ui_label(UiCtx& u, int x, int y, std::string_view text, aleph::math::Vec3 color) {
    draw_text_shadowed(*u.fb, x, y, text, color);
}

inline bool ui_button(UiCtx& u, int x, int y, int w, int h, std::string_view label) {
    const int id = x * 4096 + y;
    const bool over = in_rect(u, x, y, w, h);
    if (over) u.hot_id = id;
    bool clicked = false;
    if (u.hot_id == id && u.mouse_pressed) u.active_id = id;
    if (u.active_id == id && !u.mouse_down) {
        if (over) clicked = true;
        u.active_id = 0;
    }
    const aleph::math::Vec3 bg = (u.active_id == id) ? ui_color(45, 65, 110)
                                  : (u.hot_id == id)  ? ui_color(70, 90, 140)
                                                       : ui_color(50, 58, 80);
    draw_rect(*u.fb, x, y, w, h, bg);
    draw_rect(*u.fb, x, y, w, 1, ui_color(120, 140, 180));
    draw_rect(*u.fb, x, y + h - 1, w, 1, ui_color(20, 25, 35));
    if (!label.empty()) draw_text_shadowed(*u.fb, x + 6, y + (h - 8) / 2, label,
                                              aleph::math::Vec3{1, 1, 1});
    return clicked;
}

inline bool ui_slider_f(UiCtx& u, int x, int y, int w, int h,
                         aleph::math::f32& value, aleph::math::f32 minv, aleph::math::f32 maxv) {
    const int id = x * 4096 + y;
    const bool over = in_rect(u, x, y, w, h);
    if (over) u.hot_id = id;
    if (u.hot_id == id && u.mouse_pressed) u.active_id = id;
    bool changed = false;
    if (u.active_id == id) {
        const aleph::math::f32 t  = static_cast<aleph::math::f32>(u.mouse_x - x)
                                    / static_cast<aleph::math::f32>(w - 1);
        const aleph::math::f32 tc = std::clamp(t, 0.0f, 1.0f);
        const aleph::math::f32 nv = minv + tc * (maxv - minv);
        if (nv != value) { value = nv; changed = true; }
    }
    draw_rect(*u.fb, x, y, w, h, ui_color(20, 25, 35));
    const aleph::math::f32 t = std::clamp((value - minv) / (maxv - minv), 0.0f, 1.0f);
    const int fw = static_cast<int>(t * static_cast<aleph::math::f32>(w));
    const aleph::math::Vec3 fg = (u.active_id == id) ? ui_color(110, 160, 230)
                                  : (u.hot_id == id)  ? ui_color(90, 140, 210)
                                                       : ui_color(75, 120, 190);
    if (fw > 0) draw_rect(*u.fb, x, y, fw, h, fg);
    const int handle_x = x + fw - 2;
    if (handle_x >= x && handle_x + 4 <= x + w)
        draw_rect(*u.fb, handle_x, y - 1, 4, h + 2, ui_color(230, 240, 255));
    return changed;
}

}  // namespace aleph::editor
```

- [ ] **Step 19.4: Write `aleph.editor-orbit.cppm`**

```cpp
module;
#include <cmath>
#include <algorithm>

export module aleph.editor:orbit;

import aleph.math;
import aleph.window;

export namespace aleph::editor {

struct OrbitCam {
    aleph::math::Vec3 target{0, 0, 0};
    aleph::math::f32  yaw{0.0f};
    aleph::math::f32  pitch{0.0f};
    aleph::math::f32  radius{5.0f};
};

inline aleph::math::Vec3 orbit_eye(const OrbitCam& c) noexcept {
    return aleph::math::Vec3{
        c.target.x + c.radius * std::sin(c.yaw) * std::cos(c.pitch),
        c.target.y + c.radius * std::sin(c.pitch),
        c.target.z + c.radius * std::cos(c.yaw) * std::cos(c.pitch),
    };
}

// Returns true if camera state was modified.
inline bool orbit_handle(OrbitCam& c, const aleph::window::Event& e,
                          bool left_down, bool right_down) noexcept {
    using aleph::math::f32;
    constexpr f32 ORBIT_SPEED = 0.008f;
    constexpr f32 PAN_SPEED   = 0.012f;
    constexpr f32 ZOOM_FACTOR = 1.12f;
    switch (e.kind) {
        case aleph::window::Event::Kind::MouseMove: {
            if (left_down) {
                c.yaw   -= static_cast<f32>(e.dx) * ORBIT_SPEED;
                c.pitch -= static_cast<f32>(e.dy) * ORBIT_SPEED;
                c.pitch = std::clamp(c.pitch, -1.5f, 1.5f);
                return true;
            } else if (right_down) {
                const aleph::math::Vec3 fwd{
                    std::sin(c.yaw) * std::cos(c.pitch),
                    std::sin(c.pitch),
                    std::cos(c.yaw) * std::cos(c.pitch),
                };
                const aleph::math::Vec3 right = aleph::math::normalize(
                    aleph::math::cross(fwd, aleph::math::Vec3{0, 1, 0}));
                const aleph::math::Vec3 up = aleph::math::normalize(
                    aleph::math::cross(right, fwd));
                const f32 scale = c.radius * PAN_SPEED;
                c.target = c.target + right * (-static_cast<f32>(e.dx) * scale);
                c.target = c.target + up    * ( static_cast<f32>(e.dy) * scale);
                return true;
            }
            return false;
        }
        case aleph::window::Event::Kind::MouseWheel: {
            if (e.wheel > 0)      c.radius /= ZOOM_FACTOR;
            else if (e.wheel < 0) c.radius *= ZOOM_FACTOR;
            c.radius = std::clamp(c.radius, 0.5f, 40.0f);
            return true;
        }
        default: return false;
    }
}

}  // namespace aleph::editor
```

- [ ] **Step 19.5: Write `aleph.editor-picking.cppm`**

```cpp
module;
#include <cmath>
#include <limits>

export module aleph.editor:picking;

import aleph.math;
import aleph.render.sw;

export namespace aleph::editor {

namespace detail {

inline aleph::math::f32 face_intersect(const aleph::render::sw::Face& face,
                                         aleph::math::Vec3 orig,
                                         aleph::math::Vec3 dir) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 n  = aleph::math::cross(e1, e2);
    const aleph::math::f32 denom = aleph::math::dot(n, dir);
    if (std::abs(denom) < 1e-8f) return -1.0f;
    const aleph::math::f32 D = aleph::math::dot(n, face.verts[0]);
    const aleph::math::f32 t = (D - aleph::math::dot(n, orig)) / denom;
    if (t < 0.001f) return -1.0f;
    const aleph::math::Vec3 P = orig + dir * t;
    const aleph::math::Vec3 local = P - face.verts[0];
    const aleph::math::f32 a = aleph::math::dot(local, e1) / aleph::math::dot(e1, e1);
    const aleph::math::f32 b = aleph::math::dot(local, e2) / aleph::math::dot(e2, e2);
    if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f) return -1.0f;
    return t;
}

}  // namespace detail

inline int pick_face(const aleph::render::sw::SceneRT& sr, int sx, int sy,
                      aleph::math::Vec3 eye, aleph::math::Vec3 target,
                      aleph::math::Vec3 world_up,
                      aleph::math::f32 vfov_rad, aleph::math::f32 aspect,
                      int win_w, int win_h) noexcept {
    const aleph::math::f32 ndc_x = 2.0f * static_cast<aleph::math::f32>(sx) /
                                    static_cast<aleph::math::f32>(win_w) - 1.0f;
    const aleph::math::f32 ndc_y = 1.0f - 2.0f * static_cast<aleph::math::f32>(sy) /
                                    static_cast<aleph::math::f32>(win_h);
    const aleph::math::f32 t_y = std::tan(vfov_rad * 0.5f);
    const aleph::math::f32 t_x = t_y * aspect;
    const aleph::math::Vec3 fwd   = aleph::math::normalize(target - eye);
    const aleph::math::Vec3 right = aleph::math::normalize(aleph::math::cross(fwd, world_up));
    const aleph::math::Vec3 up    = aleph::math::normalize(aleph::math::cross(right, fwd));
    const aleph::math::Vec3 ray_dir = right * (ndc_x * t_x) + up * (ndc_y * t_y) + fwd;
    int best = -1;
    aleph::math::f32 best_t = std::numeric_limits<aleph::math::f32>::infinity();
    for (int i = 0; i < static_cast<int>(sr.faces.size()); ++i) {
        const aleph::math::f32 t = detail::face_intersect(sr.faces[i], eye, ray_dir);
        if (t > 0.0f && t < best_t) { best_t = t; best = i; }
    }
    return best;
}

}  // namespace aleph::editor
```

- [ ] **Step 19.6: Primary + CMake**

`aleph.editor.cppm`:
```cpp
export module aleph.editor;
export import :font;
export import :ui;
export import :orbit;
export import :picking;
```

`render/src/aleph.editor/CMakeLists.txt`:
```cmake
add_library(aleph_editor)
target_sources(aleph_editor
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.editor.cppm
        aleph.editor-font.cppm
        aleph.editor-ui.cppm
        aleph.editor-orbit.cppm
        aleph.editor-picking.cppm)
target_link_libraries(aleph_editor
    PUBLIC  aleph_render_common aleph_render_sw aleph_window aleph_math
    PRIVATE aleph_flags_isa)
```

Conditional `add_subdirectory` in render/CMakeLists.txt (same `if(ALEPH_HAVE_SDL2)` block).

In `tests/CMakeLists.txt`, conditionally add the editor tests:
```cmake
if(ALEPH_HAVE_SDL2)
    target_sources(aleph_tests PRIVATE
        editor/test_orbit.cpp
        editor/test_pick.cpp)
    target_link_libraries(aleph_tests PRIVATE aleph_editor)
endif()
```

- [ ] **Step 19.7: Build + test + commit**

```bash
cmake --build build-release --target aleph_tests
./build-release/tests/aleph_tests 2>&1 | tail -3
git add render/src/aleph.editor/ render/CMakeLists.txt tests/editor/test_orbit.cpp tests/editor/test_pick.cpp tests/CMakeLists.txt
git commit -m "task 19: aleph.editor — font + immediate-mode UI + orbit cam + ray-vs-quad picking"
```

---

## Task 20: `apps/aleph_rt` — CLI raytracer

**Files:**
- Create: `apps/aleph_rt/main.cpp`
- Create: `apps/aleph_rt/CMakeLists.txt`
- Modify: `apps/CMakeLists.txt` — uncomment add_subdirectory

- [ ] **Step 20.1: Write `apps/aleph_rt/main.cpp`**

```cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

import aleph.math;
import aleph.scene;
import aleph.render.rt;
import aleph.render.common;
import aleph.alloc;
import aleph.threads;
import aleph.io;
import aleph.common;

namespace {

using aleph::math::Vec3;
using aleph::math::f32;
using aleph::math::u32;
using aleph::math::u64;

void build_cover(aleph::scene::Scene& s, aleph::math::Pcg32& rng) {
    const auto m_ground = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    aleph::scene::scene_add_sphere(s, Vec3{0, -1000, 0}, 1000.0f, m_ground);
    for (int a = -11; a < 11; ++a) for (int b = -11; b < 11; ++b) {
        const f32 choose = rng.float01();
        const Vec3 c{static_cast<f32>(a) + 0.9f * rng.float01(),
                     0.2f,
                     static_cast<f32>(b) + 0.9f * rng.float01()};
        if (aleph::math::length(c - Vec3{4, 0.2f, 0}) <= 0.9f) continue;
        aleph::scene::MaterialHandle m;
        if (choose < 0.8f) {
            m = aleph::scene::scene_add_lambertian(s,
                Vec3{rng.float01() * rng.float01(),
                     rng.float01() * rng.float01(),
                     rng.float01() * rng.float01()});
        } else if (choose < 0.95f) {
            m = aleph::scene::scene_add_metal(s,
                Vec3{0.5f + 0.5f * rng.float01(),
                     0.5f + 0.5f * rng.float01(),
                     0.5f + 0.5f * rng.float01()},
                0.5f * rng.float01());
        } else {
            m = aleph::scene::scene_add_dielectric(s, 1.5f);
        }
        aleph::scene::scene_add_sphere(s, c, 0.2f, m);
    }
    aleph::scene::scene_add_sphere(s, Vec3{0, 1, 0}, 1.0f,
        aleph::scene::scene_add_dielectric(s, 1.5f));
    aleph::scene::scene_add_sphere(s, Vec3{-4, 1, 0}, 1.0f,
        aleph::scene::scene_add_lambertian(s, Vec3{0.4f, 0.2f, 0.1f}));
    aleph::scene::scene_add_sphere(s, Vec3{4, 1, 0}, 1.0f,
        aleph::scene::scene_add_metal(s, Vec3{0.7f, 0.6f, 0.5f}, 0.0f));
}

void build_cornell(aleph::scene::Scene& s) {
    const auto RED   = aleph::scene::scene_add_lambertian(s, Vec3{0.65f, 0.05f, 0.05f});
    const auto GREEN = aleph::scene::scene_add_lambertian(s, Vec3{0.12f, 0.45f, 0.15f});
    const auto WHITE = aleph::scene::scene_add_lambertian(s, Vec3{0.73f, 0.73f, 0.73f});
    const auto LIGHT = aleph::scene::scene_add_emissive  (s, Vec3{15.0f, 15.0f, 15.0f});
    aleph::scene::scene_add_quad(s, Vec3{555, 0, 0},   Vec3{0, 555, 0}, Vec3{0, 0, 555}, GREEN);
    aleph::scene::scene_add_quad(s, Vec3{0,   0, 0},   Vec3{0, 555, 0}, Vec3{0, 0, 555}, RED);
    aleph::scene::scene_add_quad(s, Vec3{343, 554, 332}, Vec3{-130, 0, 0}, Vec3{0, 0, -105}, LIGHT);
    aleph::scene::scene_add_quad(s, Vec3{0,   0,   0}, Vec3{555, 0, 0}, Vec3{0, 0, 555}, WHITE);
    aleph::scene::scene_add_quad(s, Vec3{555, 555, 0}, Vec3{-555, 0, 0}, Vec3{0, 0, 555}, WHITE);
    aleph::scene::scene_add_quad(s, Vec3{0, 0, 555},   Vec3{555, 0, 0}, Vec3{0, 555, 0}, WHITE);
    // The two boxes (Sotark parity): port the add_box helper inline here.
    auto add_box = [&](Vec3 mn, Vec3 mx, aleph::scene::MaterialHandle mat) {
        const f32 dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mx.z}, Vec3{ dx, 0, 0}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mx.x, mn.y, mn.z}, Vec3{-dx, 0, 0}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mx.x, mn.y, mx.z}, Vec3{0, 0, -dz}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mn.z}, Vec3{0, 0,  dz}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mx.y, mx.z}, Vec3{ dx, 0, 0}, Vec3{0, 0, -dz}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mn.z}, Vec3{ dx, 0, 0}, Vec3{0, 0,  dz}, mat);
    };
    add_box(Vec3{265, 0,  295}, Vec3{430, 330, 460}, WHITE);
    add_box(Vec3{130, 0,   65}, Vec3{295, 165, 230}, WHITE);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: aleph_rt <out.ppm> <cover|cornell> [spp] [depth] [seed] [threads]\n");
        return 1;
    }
    const std::string_view out_path = argv[1];
    const std::string_view scene_s  = argv[2];
    const int spp     = argc > 3 ? std::atoi(argv[3]) : 16;
    const int depth   = argc > 4 ? std::atoi(argv[4]) : 8;
    const u64 seed    = argc > 5 ? static_cast<u64>(std::atoll(argv[5])) : 42ull;
    int       threads = argc > 6 ? std::atoi(argv[6]) : 0;
    if (threads <= 0) threads = static_cast<int>(std::thread::hardware_concurrency());
    if (threads <= 0) threads = 1;

    int W = 400, H = 225;
    Vec3 lookfrom{13, 2, 3}, lookat{0, 0, 0};
    f32 vfov = 20.0f, defocus = 0.6f, focus_dist = 10.0f;
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};

    aleph::scene::Scene scene;
    if (scene_s == "cover") {
        aleph::math::Pcg32 rng(1u, 1u);
        build_cover(scene, rng);
    } else if (scene_s == "cornell") {
        W = H = 400;
        lookfrom = Vec3{278, 278, -800};
        lookat   = Vec3{278, 278, 0};
        vfov = 40.0f; defocus = 0.0f; focus_dist = 800.0f;
        sky = aleph::render::common::Sky{Vec3{0, 0, 0}, Vec3{0, 0, 0}};
        build_cornell(scene);
    } else {
        std::fprintf(stderr, "aleph_rt: unknown scene '%.*s'\n",
                      static_cast<int>(scene_s.size()), scene_s.data());
        return 1;
    }

    static unsigned char bvh_scratch[1 << 20];
    aleph::alloc::Arena bvh_arena{bvh_scratch, sizeof(bvh_scratch)};
    aleph::scene::scene_build_bvh(scene, bvh_arena);

    static alignas(64) unsigned char film_scratch[400 * 400 * sizeof(Vec3)];
    aleph::alloc::Arena film_arena{film_scratch, sizeof(film_scratch)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);

    const aleph::render::common::Camera cam = aleph::render::common::make_camera(
        lookfrom, lookat, Vec3{0, 1, 0}, vfov, W, H, defocus, focus_dist);

    aleph::threads::Pool pool(threads);
    aleph::render::rt::path_trace(scene, cam, sky, film, pool,
        aleph::render::rt::RenderOpts{spp, depth, seed, 32});

    // Convert linear Film to aleph.common::Image (gamma 2.0) for PPM writer.
    aleph::common::Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            img(x, y) = film.pixels[y * film.stride_pixels + x];

    if (auto r = aleph::common::write_ppm(img, out_path); !r) {
        std::fprintf(stderr, "aleph_rt: write_ppm failed: %s\n", r.error().c_str());
        return 1;
    }
    return 0;
}
```

- [ ] **Step 20.2: Write `apps/aleph_rt/CMakeLists.txt`**

```cmake
add_executable(aleph_rt main.cpp)
target_link_libraries(aleph_rt PRIVATE
    aleph_scene aleph_render_rt aleph_render_common
    aleph_math aleph_alloc aleph_threads aleph_io aleph_common
    aleph_flags_test)
```

Uncomment in `apps/CMakeLists.txt`.

- [ ] **Step 20.3: Build + smoke test + commit**

```bash
cd /home/lkz/aleph-cxx
cmake --build build-release --target aleph_rt 2>&1 | tail -3
./build-release/apps/aleph_rt/aleph_rt /tmp/cover.ppm cover 5 5 42 4
ls -la /tmp/cover.ppm   # should exist, >10KB
git add apps/aleph_rt/ apps/CMakeLists.txt
git commit -m "task 20: apps/aleph_rt — CLI raytracer (cover + cornell scenes)"
```

---

## Task 21: `apps/aleph_sw` — interactive SDL editor

**Files:**
- Create: `apps/aleph_sw/main.cpp`
- Create: `apps/aleph_sw/CMakeLists.txt`
- Modify: `apps/CMakeLists.txt`

- [ ] **Step 21.1: Write `apps/aleph_sw/main.cpp`** (Sotark cxx26 main.cpp adapted)

```cpp
#include <cstdio>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <cmath>

import aleph.math;
import aleph.render.common;
import aleph.render.sw;
import aleph.window;
import aleph.editor;
import aleph.alloc;
import aleph.threads;

int main(int /*argc*/, char** /*argv*/) {
    constexpr int W = 800, H = 600;
    aleph::window::Window win(W, H, "aleph_sw editor");

    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, aleph::math::Vec3{0, 0, 0}, 24.0f,
                                  aleph::render::sw::tex_floor);
    aleph::render::sw::add_pillar(sr, aleph::math::Vec3{0, 0, 0}, 0.8f, 3.5f,
                                   aleph::render::sw::tex_brick);
    aleph::render::sw::add_cube(sr, aleph::math::Vec3{3, 0.5f, 1.5f}, 1.0f,
                                 aleph::render::sw::tex_brick);
    aleph::render::sw::add_cube(sr, aleph::math::Vec3{-2.5f, 0.4f, -1.8f}, 0.8f,
                                 aleph::render::sw::tex_checker);

    // Allocate one big lightmap pool for all faces (each face gets 32x32 = 1024 u32).
    constexpr int LM = 32;
    sr.lightmap_pool.assign(sr.faces.size() * LM * LM, 0u);
    for (std::size_t i = 0; i < sr.lightmaps.size(); ++i) {
        sr.lightmaps[i].texels = sr.lightmap_pool.data() + i * LM * LM;
        sr.lightmaps[i].w = LM; sr.lightmaps[i].h = LM;
    }
    aleph::math::Vec3 light_pos{3, 8, 4};
    aleph::math::f32 intensity = 35.0f;
    aleph::math::f32 ambient   = 0.12f;
    aleph::render::sw::bake_lightmaps(sr, light_pos, intensity, ambient);

    aleph::editor::OrbitCam cam{aleph::math::Vec3{0, 1, 0}, 0.3f, 0.25f, 8.0f};
    aleph::editor::UiCtx ui{};

    aleph::threads::Pool pool(4);

    // Film aliased over SDL surface pixels (ARGB) is not Vec3, so we render
    // into a separate Vec3 Film, then tonemap into the window pixels.
    static alignas(64) unsigned char film_scratch[W * H * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena film_arena{film_scratch, sizeof(film_scratch)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);
    std::vector<aleph::math::f32> depth(W * H, 1.0f);

    bool running = true;
    int mouse_x = 0, mouse_y = 0;
    bool left_down = false, right_down = false, prev_left = false;
    int selected_face = -1;

    while (running) {
        std::array<aleph::window::Event, 64> evbuf{};
        const int nev = win.poll_events(std::span<aleph::window::Event>{evbuf});
        bool changed = false;
        bool clicked_pick = false;
        for (int i = 0; i < nev; ++i) {
            const auto& e = evbuf[i];
            switch (e.kind) {
                case aleph::window::Event::Kind::Quit: running = false; break;
                case aleph::window::Event::Kind::KeyDown:
                    if (e.key == 27 /*SDLK_ESCAPE*/) running = false;
                    break;
                case aleph::window::Event::Kind::MouseDown:
                    if (e.button == 1) { left_down = true; clicked_pick = true; }
                    if (e.button == 3) right_down = true;
                    mouse_x = e.x; mouse_y = e.y;
                    break;
                case aleph::window::Event::Kind::MouseUp:
                    if (e.button == 1) left_down = false;
                    if (e.button == 3) right_down = false;
                    break;
                case aleph::window::Event::Kind::MouseMove:
                    mouse_x = e.x; mouse_y = e.y;
                    if (aleph::editor::orbit_handle(cam, e, left_down, right_down))
                        changed = true;
                    break;
                case aleph::window::Event::Kind::MouseWheel:
                    if (aleph::editor::orbit_handle(cam, e, left_down, right_down))
                        changed = true;
                    break;
                default: break;
            }
        }

        // Clear film to sky gradient.
        for (int y = 0; y < H; ++y) {
            const aleph::math::f32 t = static_cast<aleph::math::f32>(y) /
                                        static_cast<aleph::math::f32>(H - 1);
            const aleph::math::Vec3 c =
                aleph::math::lerp(aleph::math::Vec3{0.43f, 0.55f, 0.71f},
                                    aleph::math::Vec3{0.20f, 0.31f, 0.55f}, t);
            for (int x = 0; x < W; ++x)
                film.pixels[y * film.stride_pixels + x] = c;
        }

        const aleph::math::Vec3 eye = aleph::editor::orbit_eye(cam);
        const aleph::math::Mat4 view = aleph::math::Mat4::look_at(
            eye, cam.target, aleph::math::Vec3{0, 1, 0});
        const aleph::math::Mat4 proj = aleph::math::Mat4::perspective(
            aleph::math::deg_to_rad(60.0f),
            static_cast<aleph::math::f32>(W) / static_cast<aleph::math::f32>(H),
            0.05f, 100.0f);
        const aleph::math::Mat4 mvp = proj * view;

        aleph::render::sw::rasterize(sr, mvp, film, depth, pool);

        // Picking on click-release without drag.
        if (clicked_pick && !prev_left) {
            selected_face = aleph::editor::pick_face(sr, mouse_x, mouse_y, eye, cam.target,
                aleph::math::Vec3{0, 1, 0},
                aleph::math::deg_to_rad(60.0f),
                static_cast<aleph::math::f32>(W) / static_cast<aleph::math::f32>(H),
                W, H);
        }
        prev_left = left_down;

        // UI panel
        const bool ui_mouse_pressed = left_down && !prev_left;
        aleph::editor::ui_begin(ui, &film, mouse_x, mouse_y, left_down, ui_mouse_pressed);
        aleph::editor::ui_panel(ui, W - 240, 60, 230, 180, "LIGHT");
        aleph::editor::ui_label(ui, W - 232, 90, "INTENSITY",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_int = intensity;
        aleph::editor::ui_slider_f(ui, W - 232, 106, 214, 16, intensity, 0.0f, 100.0f);
        aleph::editor::ui_label(ui, W - 232, 130, "AMBIENT",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_amb = ambient;
        aleph::editor::ui_slider_f(ui, W - 232, 146, 214, 16, ambient, 0.0f, 0.5f);
        aleph::editor::ui_label(ui, W - 232, 170, "LIGHT HEIGHT",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_ly = light_pos.y;
        aleph::editor::ui_slider_f(ui, W - 232, 186, 214, 16, light_pos.y, 1.0f, 20.0f);
        if (intensity != prev_int || ambient != prev_amb || light_pos.y != prev_ly) {
            aleph::render::sw::bake_lightmaps(sr, light_pos, intensity, ambient);
        }
        aleph::editor::ui_end(ui);

        // HUD text
        char buf[128];
        std::snprintf(buf, sizeof(buf), "FACES %zu  SELECTED %d",
                        sr.faces.size(), selected_face);
        aleph::editor::draw_rect(film, 8, 8, 360, 24, aleph::math::Vec3{0, 0, 0});
        aleph::editor::draw_text_shadowed(film, 14, 14, buf,
                                            aleph::math::Vec3{1, 1, 1});

        // Tonemap film into window pixels (ARGB8888).
        aleph::math::u32* wpx = win.pixels();
        const int wp = win.pitch_pixels();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const aleph::math::Vec3 lin = film.pixels[y * film.stride_pixels + x];
                wpx[y * wp + x] = aleph::render::common::tonemap_argb8888_gamma2(lin);
            }
        }
        win.present();
    }
    return 0;
}
```

- [ ] **Step 21.2: Write `apps/aleph_sw/CMakeLists.txt`**

```cmake
add_executable(aleph_sw main.cpp)
target_link_libraries(aleph_sw PRIVATE
    aleph_scene aleph_render_sw aleph_render_common
    aleph_window aleph_editor
    aleph_math aleph_alloc aleph_threads
    aleph_flags_test)
```

In `apps/CMakeLists.txt`, uncomment the conditional block:
```cmake
add_subdirectory(aleph_rt)
if(ALEPH_HAVE_SDL2)
    add_subdirectory(aleph_sw)
endif()
```

- [ ] **Step 21.3: Build + smoke + commit**

```bash
cmake --build build-release --target aleph_sw 2>&1 | tail -3
# Smoke: open window briefly. Skip if no DISPLAY.
if [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; then
    timeout 1 ./build-release/apps/aleph_sw/aleph_sw || true
fi
git add apps/aleph_sw/ apps/CMakeLists.txt
git commit -m "task 21: apps/aleph_sw — interactive SDL editor (orbit cam + sliders + picking)"
```

---

## Task 22: Module-isolation tests for new modules

**Files:**
- Create: `tests/isolation/iso_scene.cpp`
- Create: `tests/isolation/iso_render_common.cpp`
- Create: `tests/isolation/iso_render_rt.cpp`
- Create: `tests/isolation/iso_render_sw.cpp`
- Create (conditional): `tests/isolation/iso_window.cpp`
- Create (conditional): `tests/isolation/iso_editor.cpp`
- Modify: `tests/isolation/CMakeLists.txt`

- [ ] **Step 22.1: Write each isolation source**

`iso_scene.cpp`:
```cpp
import aleph.scene;
int main() {
    aleph::scene::Scene s;
    (void)s;
    return 0;
}
```

`iso_render_common.cpp`:
```cpp
import aleph.render.common;
import aleph.math;
int main() {
    auto c = aleph::render::common::make_camera(
        aleph::math::Vec3{0,0,5}, aleph::math::Vec3{0,0,0}, aleph::math::Vec3{0,1,0},
        60.0f, 100, 100, 0.0f, 1.0f);
    return c.has_defocus ? 1 : 0;
}
```

`iso_render_rt.cpp`:
```cpp
import aleph.render.rt;
import aleph.scene;
import aleph.math;
int main() {
    aleph::scene::Scene s;
    aleph::math::Pcg32 rng(0, 0);
    auto v = aleph::render::rt::ray_color(s, aleph::math::Ray{}, 0,
        {aleph::math::Vec3{}, aleph::math::Vec3{}}, true, rng);
    return aleph::math::length_sq(v) == 0.0f ? 0 : 1;
}
```

`iso_render_sw.cpp`:
```cpp
import aleph.render.sw;
int main() {
    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, {0,0,0}, 1.0f, aleph::render::sw::tex_floor);
    return sr.faces.size() == 1 ? 0 : 1;
}
```

`iso_window.cpp`:
```cpp
import aleph.window;
int main() {
    // Don't actually create a window — that would require DISPLAY. Just touch
    // a type to ensure link.
    aleph::window::Event e{};
    return static_cast<int>(e.kind) == 0 ? 0 : 0;   // always 0; just no-link-error means pass
}
```

`iso_editor.cpp`:
```cpp
import aleph.editor;
import aleph.math;
int main() {
    aleph::editor::OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 5.0f};
    auto e = aleph::editor::orbit_eye(c);
    return e.z >= 4.9f && e.z <= 5.1f ? 0 : 1;
}
```

- [ ] **Step 22.2: Update `tests/isolation/CMakeLists.txt`**

```cmake
function(aleph_iso_test name link)
    add_executable(iso_${name} iso_${name}.cpp)
    target_link_libraries(iso_${name} PRIVATE ${link} aleph_flags_test)
    add_test(NAME iso_${name} COMMAND iso_${name})
endfunction()

aleph_iso_test(cpu          aleph_cpu)
aleph_iso_test(math         aleph_math)
aleph_iso_test(alloc        aleph_alloc)
aleph_iso_test(containers   aleph_containers)
aleph_iso_test(io           aleph_io)
aleph_iso_test(scene        aleph_scene)
aleph_iso_test(render_common aleph_render_common)
aleph_iso_test(render_rt     aleph_render_rt)
aleph_iso_test(render_sw     aleph_render_sw)

if(ALEPH_HAVE_SDL2)
    aleph_iso_test(window aleph_window)
    aleph_iso_test(editor aleph_editor)
endif()
```

- [ ] **Step 22.3: Build + run all + commit**

```bash
cmake --build build-release
ctest --test-dir build-release --output-on-failure 2>&1 | tail -20
# Expect: 12-14 tests pass (1 aleph_tests + 9-11 iso_*)
git add tests/isolation/iso_scene.cpp tests/isolation/iso_render_common.cpp tests/isolation/iso_render_rt.cpp tests/isolation/iso_render_sw.cpp tests/isolation/iso_window.cpp tests/isolation/iso_editor.cpp tests/isolation/CMakeLists.txt
git commit -m "task 22: per-module isolation tests for scene/render.{common,rt,sw}/window/editor"
```

---

## Task 23: Bench harness extensions (render baselines)

**Files:**
- Modify: `bench/bench_main.cpp`

- [ ] **Step 23.1: Append new bench cases to `bench/bench_main.cpp`**

After the existing benchmarks, add:

```cpp
import aleph.scene;
import aleph.render.rt;
import aleph.render.sw;
import aleph.render.common;

// hit_sphere — target ≤ 12 cycles
{
    aleph::scene::SphereSoA s;
    aleph::scene::sphere_append(s, aleph::math::Vec3{0, 0, 0}, 1.0f,
        aleph::scene::MaterialHandle{aleph::scene::MaterialKind::Lambertian, 0});
    aleph::math::Ray r{aleph::math::Vec3{0, 0, -5}, aleph::math::Vec3{0, 0, 1}};
    aleph_bench::bench("hit_sphere (single)", [&](std::uint64_t iters) {
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < iters; ++i) {
            auto h = aleph::scene::detail::hit_sphere(s, 0, r, 0.001f, 1e9f);
            if (h) sink ^= static_cast<std::uint64_t>(h->t * 1000.0f);
        }
        return sink;
    });
}

// hit_quad — target ≤ 25 cycles
{
    aleph::scene::QuadSoA q;
    aleph::scene::quad_append(q, aleph::math::Vec3{-1, 0, 0},
        aleph::math::Vec3{2, 0, 0}, aleph::math::Vec3{0, 0, 2},
        aleph::scene::MaterialHandle{aleph::scene::MaterialKind::Lambertian, 0});
    aleph::math::Ray r{aleph::math::Vec3{0, 5, 1}, aleph::math::Vec3{0, -1, 0}};
    aleph_bench::bench("hit_quad (single)", [&](std::uint64_t iters) {
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < iters; ++i) {
            auto h = aleph::scene::detail::hit_quad(q, 0, r, 0.001f, 1e9f);
            if (h) sink ^= static_cast<std::uint64_t>(h->t * 1000.0f);
        }
        return sink;
    });
}

// BVH traversal (100 items, miss) — target ≤ 200 cycles
{
    aleph::scene::Scene s;
    const auto m = aleph::scene::scene_add_lambertian(s, aleph::math::Vec3{0.5f, 0.5f, 0.5f});
    aleph::math::Pcg32 rng(1, 1);
    for (int i = 0; i < 100; ++i) {
        aleph::scene::scene_add_sphere(s,
            aleph::math::Vec3{
                rng.float01() * 10.0f - 5.0f,
                rng.float01() * 10.0f - 5.0f,
                rng.float01() * 10.0f - 5.0f},
            0.05f, m);
    }
    alignas(16) static unsigned char scratch[65536];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);
    aleph::math::Ray r_miss{aleph::math::Vec3{100, 100, 100},
                              aleph::math::Vec3{1, 0, 0}};
    aleph_bench::bench("BVH traversal (100 items, miss)", [&](std::uint64_t iters) {
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < iters; ++i) {
            auto h = aleph::scene::hit(s, r_miss, 0.001f, 1e9f);
            sink ^= h.has_value();
        }
        return sink;
    });
}
```

(Skip the BVH-hit, rasterize_subspan, span_buffer_emit baselines for V1 to
keep the bench short. They can be added in a Phase 2.5 polish task. The 3
baselines above are enough to validate the scene module is in the same
performance ballpark as the foundation primitives.)

The `detail::hit_sphere` / `detail::hit_quad` functions are in an internal
namespace; to bench them either expose them as `export inline` in
`:hit`, OR write tiny exported wrappers `bench_hit_sphere(...)`.
Choose the wrapper approach to avoid widening the public API:

In `aleph.scene-hit.cppm`, append (still inside `export namespace aleph::scene`):
```cpp
[[nodiscard]] inline std::optional<HitRecord>
bench_hit_sphere(const SphereSoA& s, std::uint32_t i, aleph::math::Ray r,
                  aleph::math::f32 tmin, aleph::math::f32 tmax) noexcept {
    return detail::hit_sphere(s, i, r, tmin, tmax);
}
[[nodiscard]] inline std::optional<HitRecord>
bench_hit_quad(const QuadSoA& q, std::uint32_t i, aleph::math::Ray r,
                aleph::math::f32 tmin, aleph::math::f32 tmax) noexcept {
    return detail::hit_quad(q, i, r, tmin, tmax);
}
```

Then in the bench source, replace `aleph::scene::detail::hit_sphere(...)` with
`aleph::scene::bench_hit_sphere(...)` (same for `hit_quad`).

- [ ] **Step 23.2: Build + run + commit**

```bash
cmake --build build-bench --target aleph_bench 2>&1 | tail -3
./build-bench/bench/aleph_bench
git add bench/bench_main.cpp render/src/aleph.scene/aleph.scene-hit.cppm
git commit -m "task 23: bench: add hit_sphere / hit_quad / BVH-traversal baselines"
```

Note: baselines may exceed targets like in Phase 1 (TSC/boost mismatch). Document the measured numbers; don't lower targets.

---

## Task 24: Final validation + tag v0.2.0-render

- [ ] **Step 24.1: Full clean rebuild**

```bash
cd /home/lkz/aleph-cxx
rm -rf build-release build-release-strict build-bench
cmake --preset release-strict
cmake --build build-release-strict 2>&1 | grep -c warning:   # expect 0
cmake --preset release
cmake --build build-release 2>&1 | tail -3
ctest --test-dir build-release --output-on-failure 2>&1 | tail -15
```

Expect: all tests pass (1 aleph_tests + 9-11 iso_*).

- [ ] **Step 24.2: Smoke `aleph_rt` (cover + cornell)**

```bash
time ./build-release/apps/aleph_rt/aleph_rt /tmp/cover.ppm   cover   5  5 42 8
time ./build-release/apps/aleph_rt/aleph_rt /tmp/cornell.ppm cornell 20 10 42 8
ls -la /tmp/cover.ppm /tmp/cornell.ppm
# Both should be >= 10 KB.
```

Expect: cover <1s, cornell <5s.

- [ ] **Step 24.3: Verify cornell has visible lights**

```bash
python3 -c "
import struct
with open('/tmp/cornell.ppm','rb') as f:
    head = f.readline(); dims = f.readline().split(); maxv = f.readline()
    W,H = int(dims[0]), int(dims[1])
    px = f.read()
# Count bright (>180) pixels in upper-third (light area).
bright = sum(1 for i in range(0, W*100*3, 3) if px[i] > 180 and px[i+1] > 180)
print(f'bright pixels in upper strip: {bright}')
assert bright > 100, 'cornell light not visible'
"
```

If qemu / asan are available locally, also re-run those checks per Phase 1 criteria.

- [ ] **Step 24.4: Smoke `aleph_sw` (skip if no DISPLAY)**

```bash
if [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; then
    timeout 1 ./build-release/apps/aleph_sw/aleph_sw && echo "aleph_sw exited cleanly" || echo "aleph_sw exit non-zero (likely timeout) — OK if it didn't crash"
fi
```

- [ ] **Step 24.5: Tag the release**

```bash
git tag -a v0.2.0-render -m "$(cat <<'EOF'
aleph-cxx Phase 2 (scene & render) complete

New modules (6) + 2 executables:
  aleph.scene         SoA per kind + Handle32(kind:8,idx:24) + unified BVH
  aleph.render.common Camera + Film + tonemap + Sky
  aleph.render.rt     path tracer + NEE + scatter dispatch over MaterialKind
  aleph.render.sw     rasterizer + scanline subspan FDIV + span buffer + lightmap
  aleph.window        SDL2 wrapper (only file linking SDL2)
  aleph.editor        bitmap font + UI immediate-mode + orbit cam + picking

  apps/aleph_rt       CLI raytracer (cover + cornell scenes) → PPM
  apps/aleph_sw       interactive SDL2 editor (orbit/pick/sliders)

Stats: ~5K LOC, 135+ doctest cases, 11 isolation tests, 9 microbench baselines.

Functional parity with Sotark cxx26 reference (no `std::variant` in hot paths;
unified BVH with packed Handle32 leaves; one kind-switch per visited leaf).

Phase 3 (DPO + topology) builds on this scene representation.
EOF
)"
git log --oneline | head -10
git tag -l
```

- [ ] **Step 24.6: DO NOT push.** Push requires explicit user consent.

---

## Notes for the executing engineer

- **Order matters.** Tasks 1–17 build the libraries; 18-19 add the SDL-dependent modules (skipped cleanly if SDL2 absent); 20-21 build the executables; 22-24 verify the whole.

- **GCC 16 placement-new bug** (encountered repeatedly in Phase 1): if a `std::vector<T>` push_back triggers `__is_nothrow_new_constructible` errors in module context, fall back to the manual pattern used in Phase 1's `WorkStealingDeque` / `Pool` / `DenseIndex` (manual `::operator new` + placement-new + explicit destruction + deleted copy ops).

- **All module libraries link `aleph_flags_isa`**, NOT `aleph_flags_strict` (BMI dialect tagging; see Phase 1 spec).

- **Bench baselines may not hit targets** on first measurement due to TSC@3 GHz vs targets@4 GHz boost. Document gaps in the commit; don't lower targets without root-causing.

- **Apps directory naming**: `apps/aleph_rt/main.cpp` (no module units, just a free `int main()`). Both apps include `import aleph.math; …` directly — apps don't need to be modules themselves.

## Spec coverage check

| Spec requirement                                    | Task                |
| ---                                                 | ---                 |
| `render/` + `apps/` skeleton + SDL2 detection      | 1                   |
| `aleph.scene` (Handle32, SoA, Scene, BVH, hit)     | 2, 3, 4, 5, 6, 7, 8 |
| `aleph.render.common` (Camera, Sky, Film, tonemap) | 9, 10               |
| `aleph.render.rt` (sampling, scatter, NEE, tiles)  | 11, 12, 13          |
| `aleph.render.sw` (clip, span, rast, lightmap, prims, full rasterize) | 14, 15, 16, 17 |
| `aleph.window` (SDL wrapper)                       | 18                  |
| `aleph.editor` (font, UI, orbit, picking)          | 19                  |
| `apps/aleph_rt` (CLI)                              | 20                  |
| `apps/aleph_sw` (SDL editor)                       | 21                  |
| Module-graph isolation enforcement                 | 22                  |
| Bench microbenchmarks                              | 23                  |
| Success criteria validation + tag                  | 24                  |

