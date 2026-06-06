# Directional Sky Ambient + Sun Tint — Implementation Plan (visual slice 4b)

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** replace the flat grey ambient (`kAmbient=0.45`) in the raster bake with a hemispheric **sky** term (cool from above, by world-up normal) + a soft warm **sun tint** (half-Lambert wrap from the dominant light), AO-occluded.

**Architecture:** all in `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm` (`detail`) + `tests/edit/test_build_sw.cpp`. Reuses `aleph::math::{lerp,hadamard,normalize,dot}`, `std::clamp`/`std::fabs` (`<algorithm>`/`<cmath>` already included), the existing `light_center`, and slice-4a's `ao`. No signature/`LoweredScene` changes — `shade_face` already has `point`, `lights`, `two_sided`.

**Spec:** `docs/superpowers/specs/2026-06-06-sky-ambient-design.md` (REVISED, adversarial-reviewed — read §2/§5).

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; one case `--test-case`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0 (the lone "ninja: premature end of file" state note is NOT a compiler warning).

---

## Task 1: sky_ambient + sun_tint helpers + shade_face ambient seed

**Files:** `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`.

> After this task the build compiles and the determinism/φ/AO/contact-shadow tests pass, but the two stale-`0.45` baseline tests (`sphere front face stays lit`, `a lone sphere is not self-darkened`) WILL fail — Task 2 re-expresses them. That is expected; do not weaken Task 1 to make them pass.

- [ ] **Step 1 — constants.** Immediately after the `kAoDirs` array (build_sw.cppm:~216), before `light_center`:
```cpp
// Hemispheric sky ambient (replaces the flat grey kAmbient). Channel-sum zenith=1.47,
// horizon=1.19; the uniform-hemisphere mean (a=0.5) is ~0.443 ≈ the old 0.45 in aggregate.
// The redistribution is the feature: up-faces read brighter/cooler, horizon-faces dimmer.
inline constexpr aleph::math::Vec3 kSkyZenith  = {0.43f, 0.48f, 0.56f}; // up-facing (cool, bright)
inline constexpr aleph::math::Vec3 kSkyHorizon = {0.38f, 0.39f, 0.42f}; // side-facing (neutral, dimmer)
// Soft warm half-bounce fill from the dominant light (fake sun GI). A fill, not a key.
inline constexpr aleph::math::Vec3 kSunColor    = {0.55f, 0.42f, 0.28f}; // warm tint (scaled by w·strength)
inline constexpr aleph::math::f32  kSunStrength = 0.12f;                 // fill weight (max lum add ≈ 0.05)
```

- [ ] **Step 2 — `sky_ambient`** (in `detail`, after `light_center`, before `shade_face`):
```cpp
// Hemispheric sky ambient sampled by the WORLD-UP-reoriented normal. The quad/tri
// normal sign is arbitrary (the floor's cross(u,v)=(0,-64,0) points DOWN), so flip to
// the upper hemisphere — same reorientation AO uses. Known preview simplification: a
// closed convex surface (sphere) thus reads its BOTTOM as bright as its TOP.
[[nodiscard]] inline aleph::math::Vec3 sky_ambient(aleph::math::Vec3 N) noexcept {
    using aleph::math::Vec3;
    const Vec3 N_up = (aleph::math::dot(N, Vec3{0.0f, 1.0f, 0.0f}) < 0.0f) ? N * -1.0f : N;
    const aleph::math::f32 a = std::clamp(N_up.y, 0.0f, 1.0f);  // std:: + FLOAT literals
    return aleph::math::lerp(kSkyHorizon, kSkyZenith, a);
}
```

- [ ] **Step 3 — `sun_tint`** (in `detail`, after `sky_ambient`):
```cpp
// Soft warm fill from the dominant light — a cheap half-Lambert wrap (fake sun GI).
// On two-sided geometry (quads/tris, arbitrary winding) uses |dot| so warmth depends
// on lighting, not winding (matches the direct-light |N·L|); on a sphere uses signed
// dot so only the lit hemisphere warms. Wrap is 0 at nd=-1, 0.5 at the terminator.
[[nodiscard]] inline aleph::math::Vec3
sun_tint(aleph::math::Vec3 point, aleph::math::Vec3 N,
         const std::vector<LoweredEntity>& lights, bool two_sided) noexcept {
    using aleph::math::Vec3;
    // Primary light = max emissive luminance; STRICT '>' so the lowest index wins ties.
    aleph::math::f32 best = -1.0f;
    const LoweredEntity* primary = nullptr;
    for (const LoweredEntity& L : lights) {
        const aleph::math::f32 s = L.material.emit.x + L.material.emit.y + L.material.emit.z;
        if (s > best) { best = s; primary = &L; }
    }
    if (primary == nullptr) return Vec3{0.0f, 0.0f, 0.0f};
    // Guard BEFORE normalize (aleph::math::normalize has no zero-guard -> NaN).
    const Vec3 d = light_center(primary->world_geometry) - point;
    const aleph::math::f32 dist2 = aleph::math::dot(d, d);
    if (dist2 < 1e-6f) return Vec3{0.0f, 0.0f, 0.0f};
    const Vec3 L = d * (1.0f / std::sqrt(dist2));
    const aleph::math::f32 nd = two_sided ? std::fabs(aleph::math::dot(N, L))
                                          : aleph::math::dot(N, L);
    const aleph::math::f32 w = std::max(0.0f, 0.5f * nd + 0.5f);
    return kSunColor * (kSunStrength * w);   // Vec3 * f32 (scale) — no hadamard here
}
```

- [ ] **Step 4 — `shade_face` ambient seed.** Replace build_sw.cppm:438:
```cpp
aleph::math::Vec3 lit = base_albedo * (kAmbient * ao) + self_emit;
```
with:
```cpp
const aleph::math::Vec3 amb = sky_ambient(N) + sun_tint(point, N, lights, two_sided);
aleph::math::Vec3 lit = aleph::math::hadamard(base_albedo, amb) * ao + self_emit;
```
**Use `hadamard`, NOT `*`** — `operator*(Vec3,Vec3)` is the geometric product (→`Multivector`); per-channel albedo×color is `aleph::math::hadamard` (the existing idiom at build_sw.cppm:450). `ao`, unit `N`, `point`, `lights`, `two_sided` are all already in scope. Do NOT delete `kAmbient` yet (tests still reference it; that keeps it warning-free).

- [ ] **Step 5 — build + run.** `cmake --build build-release` then `./build-release/tests/aleph_tests --test-case="build_sw*"`. Expect: determinism (`same_face`), φ, AO oracle, contact-shadow tests PASS; the two baseline tests (`sphere front face stays lit`, `a lone sphere is not self-darkened`) FAIL on the stale `0.45` baseline (Task 2 fixes). Strict gate: `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0.

- [ ] **Step 6 — commit.** `feat(sw): directional sky ambient + warm sun tint on the ambient seed`.

---

## Task 2: discriminating oracles + baseline re-expression + artifact

**Files:** `tests/edit/test_build_sw.cpp`.

> The `detail` namespace is exported, so tests may call `aleph::lowering::detail::sky_ambient/sun_tint`. Mirror the existing direct calls (e.g. the slice-4a `ambient_occlusion` calls). `lum`, `face_centroid`, `Vec3` helpers already exist.

- [ ] **Step 1 — hemispheric (sky) oracle.** New `namespace`-local scene helper + case. A NEUTRAL sphere (albedo.z>0 REQUIRED — a red sphere collapses the blue oracle) at the origin, NO lights:
```cpp
namespace {
aleph::lowering::LoweredScene make_sky_scene() {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;  // NO lights -> ambient is sky only
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
    sphere.material.albedo = Vec3{0.7f, 0.7f, 0.7f};   // neutral: albedo.z>0
    ls.entities.push_back(sphere);
    return ls;
}
}  // namespace

TEST_CASE("build_sw: hemispheric sky ambient — up-faces are brighter and bluer") {
    const aleph::lowering::LoweredScene ls = make_sky_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // Sphere normal at a face = normalize(centroid - centre); centre is the origin.
    // Pick the most up-facing (top, N.y->+1 -> zenith) and a near-equator face
    // (|N.y| smallest -> horizon). Compare top vs EQUATOR (NOT bottom: the world-up
    // flip makes bottom==top).
    aleph::math::f32 top_ny = -1.0f, eq_ny = 2.0f;
    Vec3 top_c{}, eq_c{};
    bool found_top = false, found_eq = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const Vec3 c = face_centroid(sw.scene.faces[i]);
        const Vec3 N = aleph::math::normalize(c);  // centre at origin
        if (N.y > top_ny) { top_ny = N.y; top_c = sw.scene.faces[i].vcol[0]; found_top = true; }
        if (std::fabs(N.y) < eq_ny) { eq_ny = std::fabs(N.y); eq_c = sw.scene.faces[i].vcol[0]; found_eq = true; }
    }
    REQUIRE(found_top);
    REQUIRE(found_eq);
    // Brighter (albedo-agnostic) AND bluer (needs albedo.z>0) at the zenith.
    CHECK(lum(top_c) > lum(eq_c));
    CHECK(top_c.z / lum(top_c) > eq_c.z / lum(eq_c));
}
```

- [ ] **Step 2 — sun-tint (warmth + direction) oracle.** A SPHERE (two_sided=false) so shadowed-hemisphere vertices skip the direct light; one light off to +X; compare two EQUATOR vertices (same N.y so the sky gradient cancels), the more-toward-light one is warmer:
```cpp
TEST_CASE("build_sw: sun tint warms the equator vertices facing the light") {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
    sphere.material.albedo = Vec3{0.7f, 0.7f, 0.7f};   // neutral
    ls.entities.push_back(sphere);
    LoweredEntity light;                               // off to +X
    light.source = NodeId{100};
    light.world_geometry = QuadLocal{Vec3{5.0f, -0.5f, -0.5f}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    light.material.emit = Vec3{3.0f, 3.0f, 3.0f};
    ls.lights.push_back(light);

    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);
    const Vec3 Lc = Vec3{5.0f, 0.0f, 0.0f};  // light centre (for dot(N,L) classification)

    // Among near-equator faces (|N.y|<0.15, same sky_ambient) that are SHADOWED
    // (dot(N,L)<=0, so direct light is skipped on the two_sided=false sphere), pick
    // the one most-toward the light (max dot, grazing) and most-away (min dot).
    aleph::math::f32 toward_dot = -2.0f, away_dot = 2.0f;
    Vec3 toward_c{}, away_c{};
    bool found_t = false, found_a = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const Vec3 c = face_centroid(sw.scene.faces[i]);
        const Vec3 N = aleph::math::normalize(c);
        if (std::fabs(N.y) > 0.15f) continue;                 // equator ring only
        const Vec3 L = aleph::math::normalize(Lc - c);
        const aleph::math::f32 nl = aleph::math::dot(N, L);
        if (nl > 0.0f) continue;                              // shadowed side only
        if (nl > toward_dot) { toward_dot = nl; toward_c = sw.scene.faces[i].vcol[0]; found_t = true; }
        if (nl < away_dot)   { away_dot   = nl; away_c   = sw.scene.faces[i].vcol[0]; found_a = true; }
    }
    REQUIRE(found_t);
    REQUIRE(found_a);
    REQUIRE(toward_dot <= 0.0f);   // both genuinely shadowed -> ambient-only vcol
    // The vertex more toward the light gets the larger warm wrap -> higher red fraction.
    CHECK(toward_c.x / lum(toward_c) > away_c.x / lum(away_c));
}
```

- [ ] **Step 3 — re-express the lone-sphere baseline** (`test_build_sw.cpp:448-480`). The sphere is centred at the origin, so `n0 = normalize(v0)` equals `shade_face`'s `N`; lights empty → `sun_tint=0`; AO==1. Replace the single-scalar `expect_ambient_lum` with a per-face recompute via the helpers:
```cpp
        const Vec3 v0 = sw.scene.faces[i].verts[0];
        const Vec3 n0 = aleph::math::normalize(v0);
        const aleph::math::f32 ao =
            aleph::lowering::detail::ambient_occlusion(v0, n0, ls.entities, NodeId{1});
        CHECK(ao == doctest::Approx(1.0f));
        // Directional ambient: expected lum depends on this face's own normal. No
        // lights -> sun_tint=0; AO==1; centre at origin so n0 == shade_face's N.
        const Vec3 amb = aleph::lowering::detail::sky_ambient(n0);
        const Vec3 expect =
            aleph::math::hadamard(sphere.material.albedo, amb)
            * aleph::lowering::detail::kRasterExposure;
        CHECK(lum(sw.scene.faces[i].vcol[0]) == doctest::Approx(lum(expect)));
        checked = true;
        break;
```
(Remove the old `expect_ambient_lum` scalar at lines 462-464. Keep `sphere` in scope — it already is, constructed locally in that test.)

- [ ] **Step 4 — re-express the sphere-lit baseline** (`test_build_sw.cpp:319-352`). Replace the hardcoded `ambient_lum = (0.8+0.2+0.2)*0.45` (which also omitted exposure) with the top vertex's OWN ambient seed, computed WITH exposure + the real lights, via the helpers. The albedo is the shadow scene's sphere `{0.8,0.2,0.2}`, `two_sided=false` (sphere):
```cpp
    // The lit top must exceed its OWN ambient seed (sky + sun, AO==1, exposed).
    const Vec3 N_top = aleph::math::normalize(top_pt - Vec3{0.0f, 0.8f, 0.0f});  // sphere centre
    const aleph::math::f32 sphere_ao =
        aleph::lowering::detail::ambient_occlusion(top_pt, N_top, ls.entities, NodeId{1});
    CHECK(sphere_ao == doctest::Approx(1.0f));
    const Vec3 amb_top = aleph::lowering::detail::sky_ambient(N_top)
        + aleph::lowering::detail::sun_tint(top_pt, N_top, ls.lights, /*two_sided=*/false);
    const Vec3 seed_top = aleph::math::hadamard(Vec3{0.8f, 0.2f, 0.2f}, amb_top)
        * aleph::lowering::detail::kRasterExposure;
    CHECK(top_lum > lum(seed_top));   // direct light survives -> the top is genuinely lit
```
(Delete the old `ambient_lum` lines 349-351. `top_pt` is already captured by the loop above.)

- [ ] **Step 5 — delete `kAmbient`.** `grep -n 'kAmbient' bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm tests/edit/test_build_sw.cpp` — only comments/the bare literal should remain. Delete the `inline constexpr aleph::math::f32 kAmbient = 0.45f;` line (build_sw.cppm:141) and its comment (it is dead; namespace-scope `inline constexpr` does not warn, so this is cleanliness). Fix any comment that still says "kAmbient".

- [ ] **Step 6 — build + run all + strict.** `cmake --build build-release && ctest --test-dir build-release` → all pass (was 20 suites). `--test-case="build_sw*"` → all green incl. the 2 new + 2 re-expressed. Strict warnings → 0. Confirm `same_face` determinism + φ-skip still pass.

- [ ] **Step 7 — before/after artifact.** Render headless `step1_add_object` raster on this branch (+sky/sun) vs `main` (flat grey ambient); montage to `docs/superpowers/artifacts/2026-06-06-sky-ambient-before-after.png` (use a temp `git worktree add /tmp/wt-main main` for the "before"; clean it up). Expect cool sky tint on up-faces + a warm fill near the light-side terminator. If awkward, render the +sky frame and note it.

- [ ] **Step 8 — commit.** `test(sw): sky-ambient oracles (hemispheric + sun-tint) + baseline re-expression + artifact`.

---

## Final verification
- [ ] Hemispheric oracle (`lum(top)>lum(eq)` AND bluer) + sun-tint oracle (toward warmer) pass; AO oracle still `lum(near)<lum(far)`; determinism `same_face` + φ-skip pass.
- [ ] `ctest` all pass; release-strict 0 warnings. `--wave` unaffected (ambient φ-skipped — optional `cmp` of two `--wave` PPMs).
- [ ] Before/after artifact shows the directional sky tint + warm sun fill replacing flat grey.
