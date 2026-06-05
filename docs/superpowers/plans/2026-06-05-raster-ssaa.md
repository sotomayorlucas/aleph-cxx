# Raster SSAA Implementation Plan (visual-alignment slice 1)

> **For agentic workers:** implement task-by-task; steps use `- [ ]`. TDD, frequent commits.

**Goal:** 2×2 supersample the raster preview, box-downsample in linear space into the 1× film, so silhouettes stop being jagged. **Spec:** `docs/superpowers/specs/2026-06-05-raster-ssaa-design.md`.

**Conventions:** build+test `cmake --build build-release && ctest --test-dir build-release`; one case `./build-release/tests/aleph_tests --test-case="<name>"`; strict gate `cmake --build build-release-strict 2>&1 | grep -c "warning:"` → `0`. C++26 modules; `aleph_flags_isa` (no exceptions; `assert` ok in modules' debug, tests link `aleph_flags_test`).

---

## Task 1: `downsample_box` primitive + unit test

**Files:** add to `render/src/aleph.render.sw/` (a new partition `aleph.render.sw-downsample.cppm`, registered in that dir's `CMakeLists.txt` FILE_SET + re-exported from the `aleph.render.sw.cppm` primary like the other partitions); test `tests/render/test_sw_downsample.cpp` (add to `tests/CMakeLists.txt`).

- [ ] **Step 1 — failing test** `tests/render/test_sw_downsample.cpp`:
```cpp
#include "doctest.h"
#include <vector>
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
using aleph::math::Vec3;
using aleph::render::common::Film;

TEST_CASE("downsample_box ss=2 averages each 2x2 block (linear)") {
    // 4x4 src, 2x2 dst. Block (0,0) = {0,2,4,6}->avg 3; fill blocks with known means.
    std::vector<Vec3> s(16), d(4);
    auto at=[&](int x,int y)->Vec3&{ return s[static_cast<std::size_t>(y*4+x)]; };
    // top-left 2x2 block all = 1; top-right all = 2; bottom-left = 3; bottom-right = 4
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ at(x,y)=Vec3{1,1,1}; at(x+2,y)=Vec3{2,2,2}; at(x,y+2)=Vec3{3,3,3}; at(x+2,y+2)=Vec3{4,4,4}; }
    Film src{s.data(),4,4,4}; Film dst{d.data(),2,2,2};
    aleph::render::sw::downsample_box(src, dst, 2);
    CHECK(d[0].x==doctest::Approx(1.0f)); CHECK(d[1].x==doctest::Approx(2.0f));
    CHECK(d[2].x==doctest::Approx(3.0f)); CHECK(d[3].x==doctest::Approx(4.0f));
}
TEST_CASE("downsample_box ss=2 averages a non-uniform block") {
    std::vector<Vec3> s(16,Vec3{0,0,0}), d(4);
    s[0]=Vec3{0,0,0}; s[1]=Vec3{2,0,0}; s[4]=Vec3{4,0,0}; s[5]=Vec3{6,0,0}; // block(0,0)
    Film src{s.data(),4,4,4}; Film dst{d.data(),2,2,2};
    aleph::render::sw::downsample_box(src, dst, 2);
    CHECK(d[0].x==doctest::Approx(3.0f));   // (0+2+4+6)/4
}
TEST_CASE("downsample_box ss=1 is identity copy") {
    std::vector<Vec3> s{Vec3{1,2,3},Vec3{4,5,6}}, d(2);
    Film src{s.data(),2,1,2}; Film dst{d.data(),2,1,2};
    aleph::render::sw::downsample_box(src, dst, 1);
    CHECK(d[0].x==doctest::Approx(1.0f)); CHECK(d[1].y==doctest::Approx(5.0f));
}
```

- [ ] **Step 2 — run, expect fail** (`downsample_box` undefined).
- [ ] **Step 3 — implement** `aleph.render.sw-downsample.cppm`:
```cpp
module;
#include <cstddef>
export module aleph.render.sw:downsample;
import aleph.math;
import aleph.render.common;   // Film
export namespace aleph::render::sw {
// Average each ss×ss block of `src` (ss*dst.width × ss*dst.height) into `dst`,
// indexing rows by each film's own stride_pixels. Linear Vec3 average (pre-tonemap),
// fixed summation order → deterministic. ss==1 is an exact copy.
inline void downsample_box(const aleph::render::common::Film& src,
                           aleph::render::common::Film& dst, int ss) noexcept {
    const aleph::math::f32 inv = 1.0f / static_cast<aleph::math::f32>(ss * ss);
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            aleph::math::Vec3 acc{0.0f, 0.0f, 0.0f};
            for (int j = 0; j < ss; ++j) {
                const int sy = y * ss + j;
                for (int i = 0; i < ss; ++i)
                    acc = acc + src.pixels[sy * src.stride_pixels + (x * ss + i)];
            }
            dst.pixels[y * dst.stride_pixels + x] = acc * inv;
        }
    }
}
}  // namespace aleph::render::sw
```
(Confirm `Film` field names — `pixels`, `width`, `height`, `stride_pixels` — and that `Vec3 + Vec3` / `Vec3 * f32` exist; adapt if the real ops differ.)

- [ ] **Step 4 — register** the partition in `render/src/aleph.render.sw/CMakeLists.txt` (FILE_SET list) and add `export import :downsample;` to `render/src/aleph.render.sw/aleph.render.sw.cppm`. Add `render/test_sw_downsample.cpp` to `tests/CMakeLists.txt`.
- [ ] **Step 5 — run, expect pass**; full `ctest`; strict gate 0. **Commit** `feat(sw): downsample_box (linear box filter) for SSAA`.

---

## Task 2: SSAA in the headless raster paths (`run_headless`, `run_wave`)

**Files:** `apps/aleph_edit/main.cpp`.

- [ ] **Step 1** — add `constexpr int kSSAA = 2;` near the top of the anon namespace (next to other constants).
- [ ] **Step 2** — `run_headless`: alongside `film_px`/`depth`, add `std::vector<Vec3> ss_px(kSSAA*kSSAA*W*H);` and `std::vector<f32> ss_depth(kSSAA*kSSAA*W*H, 0.0f);` and `aleph::render::common::Film ss_film{ss_px.data(), kSSAA*W, kSSAA*H, kSSAA*W};`. In the `dump` raster step replace:
```cpp
clear_sky(film); std::fill(depth.begin(),depth.end(),0.0f);
rasterize(controller.raster_scene(), orbit_mvp(controller.camera(), W, H), film, depth, pool);
```
with:
```cpp
clear_sky(ss_film); std::fill(ss_depth.begin(),ss_depth.end(),0.0f);
rasterize(controller.raster_scene(), orbit_mvp(controller.camera(), kSSAA*W, kSSAA*H), ss_film, ss_depth, pool);
aleph::render::sw::downsample_box(ss_film, film, kSSAA);
```
NOTE: pass the SAME mvp — `orbit_mvp(cam, kSSAA*W, kSSAA*H)` keeps aspect `(2W)/(2H)==W/H`, so it equals the 1× mvp; either arg form is fine since aspect is preserved (use `kSSAA*W, kSSAA*H` for clarity that we render at SS res). The path-trace step is UNCHANGED.
- [ ] **Step 3** — `run_wave`: same transform on its `dump` lambda (it has its own `film`/`depth`; add `ss_film`/`ss_depth` once before the loop, downsample into `film`).
- [ ] **Step 4** — build app; run `--headless /tmp/h` and `--wave /tmp/w` (no crash). **Determinism:** run `--wave` twice, `cmp` a mid frame → byte-identical. **Commit** `feat(sim): 2x SSAA in headless raster (headless + wave)`.

---

## Task 3: SSAA in `run_live` + before/after artifact

**Files:** `apps/aleph_edit/main.cpp`, `tools/visual_review.sh` (optional).

- [ ] **Step 1** — `run_live`: add `ss_px`/`ss_depth`/`ss_film` (sized `kSSAA²·W·H`) next to `film_px`. In the raster branch, render the SCENE into `ss_film` then downsample into `film` BEFORE the UI overlay block:
```cpp
clear_sky(ss_film); std::fill(ss_depth.begin(),ss_depth.end(),0.0f);
rasterize(controller.raster_scene(), orbit_mvp(controller.camera(), kSSAA*W, kSSAA*H), ss_film, ss_depth, pool);
aleph::render::sw::downsample_box(ss_film, film, kSSAA);
// (existing ui_begin/ui_panel/.../draw_text_shadowed block stays, now drawing on the 1x `film`)
```
Leave the path-trace (idle) branch and the wave `step` call UNCHANGED.
- [ ] **Step 2** — build `aleph_edit_app`; probe `timeout 4 ./build-release/apps/aleph_edit/aleph_edit` (exit 124 = ran) and `--wave-live` likewise. ctest 20/20; strict 0.
- [ ] **Step 3** — before/after artifact: render `--headless` on this branch vs a 1× capture (or just capture step1 add_object raster), convert to PNG, montage a before(main)/after(ssaa) of one frame. Commit the PNG to `docs/superpowers/artifacts/2026-06-05-raster-ssaa-before-after.png`. **Commit** `feat: SSAA in live editor + before/after artifact`.

---

## Final verification
- [ ] `ctest --test-dir build-release` all pass; release-strict 0 warnings.
- [ ] `--wave` frames byte-identical across two runs (determinism preserved).
- [ ] Visual: silhouettes visibly smoother (the before/after artifact).
