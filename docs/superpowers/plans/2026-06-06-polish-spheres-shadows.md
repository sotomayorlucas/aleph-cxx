# Polish: Finer Spheres + Softer Shadows — Implementation Plan (visual slice 4d)

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** raise three `constexpr` resolution constants in `build_sw` — `kSphereRings 12→20`, `kSphereSectors 16→28`, `kShadowSamples 2→4` — over loops/arrays that are already general, for rounder spheres + a smoother shadow penumbra.

**Architecture:** one `constexpr` change block in `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm` + two test additions in `tests/edit/test_build_sw.cpp`. No new math, no signatures. `emit_sphere`/`on_sphere` already loop over `SPHERE_RINGS`/`SPHERE_SECTORS` (constant aliases, verified — no literals); `light_visibility` already sizes its sample buffer `std::array<…, kShadowSamples*kShadowSamples>` and loops `i<kShadowSamples`.

**Spec:** `docs/superpowers/specs/2026-06-06-polish-spheres-shadows-design.md` (REVISED, reviewed). **Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `--test-case`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0.

---

## Task 1: bump constants + smoothness pin + softer-shadow granularity oracle

**Files:** `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`, `tests/edit/test_build_sw.cpp`.

- [ ] **Step 1 — bump the three constants.** In build_sw.cppm: `kSphereRings 12→20` (line 87), `kSphereSectors 16→28` (line 88), `kShadowSamples 2→4` (line 190). Update each trailing comment (e.g. `// was 12`). Do a final `grep -n "\b12\b\|\b16\b" ` over `emit_sphere`/`on_sphere` and `\b2\b` over `light_visibility` to confirm NO hard-coded literal shadows the constants (the review verified none — re-confirm after editing).

- [ ] **Step 2 — build + run existing build_sw cases.** `cmake --build build-release` then `./build-release/tests/aleph_tests --test-case="build_sw*"`. Expect ALL pass unchanged: the face-count oracle computes `kSphereRings*kSphereSectors*2` from the constants (now 20·28·2=1120) so it auto-tracks; contact-shadow (`lum_under<lum_far`), AO (`lum_near<lum_far`), `same_face` determinism, and φ-skip all hold. If the face-count test fails, a literal was hidden — fix the source, not the test.

- [ ] **Step 3 — add the smoothness pin** to the existing `1 sphere + 1 quad → SPEC face counts` test (after the face-count CHECKs, ~test_build_sw.cpp:166):
```cpp
    // Polish slice 4d: pin the finer-sphere target so an accidental down-tune is
    // caught, plus a geometric "rounder than before" floor (old 12 rings ≈0.262 rad).
    CHECK(aleph::lowering::kSphereRings   >= 20);
    CHECK(aleph::lowering::kSphereSectors >= 28);
    const double polar_step = 3.14159265358979323846 /
                              static_cast<double>(aleph::lowering::kSphereRings);
    CHECK(polar_step <= 0.18);   // 20 rings ≈0.157 passes; old 12 rings ≈0.262 fails
```
(`kSphereRings`/`kSphereSectors` are exported from `aleph.lowering` — the test already uses `aleph::lowering::kSphereRings` in the face-count formula, confirm the qualifier.)

- [ ] **Step 4 — write the failing softer-shadow granularity oracle.** New case after the contact-shadow tests (near test_build_sw.cpp:317). It probes `detail::light_visibility` directly (geometry-robust — NOT a luminance band):
```cpp
TEST_CASE("build_sw: softer shadows resolve 1/16-step penumbra visibility (4x4 grid)") {
    using aleph::math::f32;
    const aleph::lowering::LoweredScene ls = make_shadow_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);
    REQUIRE(ls.lights.size() == 1u);
    // The 4x4 (16-sample) area grid yields penumbra visibility in 1/16ths — a value
    // the old 2x2 (quarter-step) grid could not produce. Probe light_visibility at
    // floor-face vertices and assert at least one genuine 16th-but-not-quarter step
    // (e.g. the penumbra corner's 10/16 = 0.625). Direct granularity proof; no
    // dependence on luminance/atten or how many cells fall in the penumbra.
    bool found_sixteenth = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{2})) continue;             // floor faces
        const Vec3 p = sw.scene.faces[i].verts[0];
        const f32 v = aleph::lowering::detail::light_visibility(
            p, Vec3{0.0f, 1.0f, 0.0f}, ls.lights[0], ls.entities, NodeId{2});
        if (v <= 0.001f || v >= 0.999f) continue;                    // penumbra only
        const f32 s16 = v * 16.0f, s4 = v * 4.0f;
        if (std::fabs(s16 - std::round(s16)) < 1e-3f &&              // a 16th-step
            std::fabs(s4  - std::round(s4))  > 1e-3f) {              // not a quarter-step
            found_sixteenth = true; break;
        }
    }
    CHECK(found_sixteenth);
    CHECK(aleph::lowering::detail::kShadowSamples >= 4);
}
```
(`detail::light_visibility` + `detail::kShadowSamples` are reachable — the `detail` namespace is exported, as the AO/sky tests already use `detail::ambient_occlusion`/`detail::sky_ambient`. `std::round`/`std::fabs` from `<cmath>` already included. `Vec3`/`NodeId`/`lum`/`make_shadow_scene` already in this file.)

- [ ] **Step 5 — build + run.** `--test-case="build_sw*"` → all green incl. the new granularity case + the smoothness pin. Full `ctest --test-dir build-release` → all suites pass. Strict: `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0. Confirm `same_face` determinism + φ-skip still pass.

- [ ] **Step 6 — before/after artifact.** Headless `step1_add_object` raster on this branch (20×28 + 4×4) vs `main` (12×16 + 2×2); montage to `docs/superpowers/artifacts/2026-06-06-polish-spheres-shadows-before-after.png` (temp `git worktree add /tmp/wt-main main` for the "before"; build + render there; `git worktree remove` after). See `tools/visual_review.sh` / the prior slices' artifacts for the headless render + montage. Expect a rounder sphere silhouette + a smoother (less stair-stepped) contact-shadow edge.

- [ ] **Step 7 — commit.** `feat(sw): finer spheres (20x28) + softer shadows (4x4) + granularity oracle`.

---

## Final verification
- [ ] `build_sw*` all green: face-count auto-tracks (1120 sphere faces), smoothness pin holds, the 1/16-step granularity oracle passes, contact-shadow + AO + `same_face` + φ unchanged.
- [ ] `ctest` all suites pass; release-strict 0 warnings.
- [ ] Before/after artifact shows rounder spheres + smoother penumbra.
