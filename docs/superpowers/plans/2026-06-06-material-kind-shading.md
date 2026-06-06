# Material-Kind Raster Shading — Implementation Plan (visual slice 4c-i)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** shade the raster preview by MaterialKind — Metal (signed sky↔ground reflection + Blinn-Phong), Dielectric (Schlick Fresnel bright rim + dim center), Lambertian/Emissive unchanged — with the eye plumbed into the bake, baked at the framed pose + per-edit.

**Spec:** `docs/superpowers/specs/2026-06-06-material-kind-shading-design.md` (REVISED-2 — read §1/§2/§5). **Scope:** shading + framed-pose bake (correct in headless + per-edit). Live continuous-orbit tracking is follow-up 4c-i-b (NOT in this plan).

**Conventions:** `cmake --build build-release && ctest --test-dir build-release`; `--test-case="build_sw*"`/`"controller*"`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0.

---

## Task 1: build_sw shading — overload, eye threading, helpers, kind branch

**Files:** `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`.

- [ ] **Step 1 — constants** after the sky/sun constants:
```cpp
inline constexpr aleph::math::Vec3 kSkyGround    = {0.10f, 0.10f, 0.12f};
inline constexpr aleph::math::f32  kMetalShinSharp = 16.0f;
inline constexpr aleph::math::f32  kMetalShinBroad =  4.0f;
inline constexpr aleph::math::f32  kMetalDiffuse   = 0.15f;
inline constexpr aleph::math::f32  kGlassShininess = 24.0f;
inline constexpr aleph::math::Vec3 kGlassCenter  = {0.22f, 0.25f, 0.30f};
inline constexpr aleph::math::Vec3 kRimColor     = {1.30f, 1.35f, 1.45f};
inline constexpr aleph::math::f32  kGlassAlbedoMix = 0.15f;
```

- [ ] **Step 2 — helpers** (`detail`, after `sun_tint`, before/near `shade_face`):
```cpp
// Signed reflection environment: keeps R.y's sign (unlike sky_ambient's world-up
// fold), so a metal facet reflecting DOWN reads the dark ground. Continuous at the
// horizon (both segments give kSkyHorizon at a=0.5).
[[nodiscard]] inline aleph::math::Vec3 sky_env(aleph::math::Vec3 R) noexcept {
    const aleph::math::f32 a = std::clamp(0.5f * R.y + 0.5f, 0.0f, 1.0f);
    return (a < 0.5f) ? aleph::math::lerp(kSkyGround, kSkyHorizon, a * 2.0f)
                      : aleph::math::lerp(kSkyHorizon, kSkyZenith, (a - 0.5f) * 2.0f);
}
// Schlick Fresnel — MANUAL quintic (std::pow is not bit-stable; mirrors render.rt).
[[nodiscard]] inline aleph::math::f32 schlick_f32(aleph::math::f32 cosine, aleph::math::f32 ior) noexcept {
    aleph::math::f32 r0 = (1.0f - ior) / (1.0f + ior); r0 = r0 * r0;
    const aleph::math::f32 oc = std::max(0.0f, 1.0f - cosine);
    return r0 + (1.0f - r0) * oc * oc * oc * oc * oc;
}
```

- [ ] **Step 3 — `shade_face` signature + kind branch.** Replace the whole function (build_sw.cppm:469-501) with — the **Lambertian/Emissive branch is the EXISTING body verbatim** (byte-identity is gated by the §5 golden):
```cpp
[[nodiscard]] inline aleph::math::Vec3
shade_face(aleph::math::Vec3 point, aleph::math::Vec3 normal,
           const MaterialParams& mat, aleph::math::Vec3 eye,
           const std::vector<LoweredEntity>& lights, bool two_sided,
           const std::vector<LoweredEntity>& occluders,
           aleph::types::NodeId self) noexcept {
    const aleph::math::Vec3 base_albedo = mat.albedo;
    const aleph::math::Vec3 self_emit   = mat.emit;
    const aleph::math::f32  nlen = aleph::math::length(normal);
    const aleph::math::Vec3 N = (nlen > 1e-8f) ? normal * (1.0f / nlen)
                                               : aleph::math::Vec3{0.0f, 1.0f, 0.0f};
    const aleph::math::f32  ao = ambient_occlusion(point, N, occluders, self);
    const aleph::math::Vec3 ev = eye - point;                              // view vector
    const aleph::math::f32  evlen = aleph::math::length(ev);
    const aleph::math::Vec3 V = (evlen > 1e-8f) ? ev * (1.0f / evlen) : N;

    if (mat.kind == aleph::types::MaterialKind::Metal) {
        const aleph::math::Vec3 R = aleph::math::reflect(V * -1.0f, N);     // mirror of incident view
        aleph::math::Vec3 lit = aleph::math::hadamard(base_albedo, sky_env(R)) * ao;
        const aleph::math::f32 shininess =
            kMetalShinSharp + (kMetalShinBroad - kMetalShinSharp) * std::clamp(mat.fuzz, 0.0f, 1.0f);
        for (const LoweredEntity& L : lights) {
            const aleph::math::Vec3 d = light_center(L.world_geometry) - point;
            const aleph::math::f32  dist_sq = aleph::math::dot(d, d);
            if (dist_sq < 1e-6f) continue;
            const aleph::math::Vec3 Ldir = d * (1.0f / std::sqrt(dist_sq));
            const aleph::math::f32  ndl = aleph::math::dot(N, Ldir);
            if (ndl <= 0.0f) continue;
            const aleph::math::f32  atten = 1.0f / (1.0f + kFall * dist_sq);
            const aleph::math::f32  vis = light_visibility(point, N, L, occluders, self);
            const aleph::math::Vec3 H0 = Ldir + V;
            const aleph::math::f32  hlen = aleph::math::length(H0);
            aleph::math::f32 spec = 0.0f;
            if (hlen > 1e-8f) {
                const aleph::math::f32 ndh = aleph::math::dot(N, H0 * (1.0f / hlen));
                if (ndh > 0.0f) spec = std::pow(ndh, shininess);
            }
            const aleph::math::f32 kk = (spec + kMetalDiffuse * ndl) * atten * kLightScale * vis;
            lit = lit + aleph::math::hadamard(base_albedo, L.material.emit) * kk;
        }
        return lit * kRasterExposure;
    }

    if (mat.kind == aleph::types::MaterialKind::Dielectric) {
        const aleph::math::f32  cosV = std::fabs(aleph::math::dot(N, V));
        const aleph::math::f32  F = schlick_f32(cosV, mat.ior);
        const aleph::math::Vec3 center = kGlassCenter + base_albedo * kGlassAlbedoMix;
        aleph::math::Vec3 lit = aleph::math::lerp(center, kRimColor, F) * ao;
        for (const LoweredEntity& L : lights) {
            const aleph::math::Vec3 d = light_center(L.world_geometry) - point;
            const aleph::math::f32  dist_sq = aleph::math::dot(d, d);
            if (dist_sq < 1e-6f) continue;
            const aleph::math::Vec3 Ldir = d * (1.0f / std::sqrt(dist_sq));
            const aleph::math::f32  atten = 1.0f / (1.0f + kFall * dist_sq);
            const aleph::math::f32  vis = light_visibility(point, N, L, occluders, self);
            const aleph::math::Vec3 H0 = Ldir + V;
            const aleph::math::f32  hlen = aleph::math::length(H0);
            if (hlen <= 1e-8f) continue;
            const aleph::math::f32  ndh = aleph::math::dot(N, H0 * (1.0f / hlen));
            if (ndh <= 0.0f) continue;
            const aleph::math::f32  spec = std::pow(ndh, kGlassShininess) * F;
            lit = lit + L.material.emit * (spec * atten * kLightScale * vis);
        }
        return lit * kRasterExposure;
    }

    // Lambertian / Emissive (default) — BYTE-FOR-BYTE the existing shade (V unused).
    const aleph::math::Vec3 amb = sky_ambient(N) + sun_tint(point, N, lights, two_sided);
    aleph::math::Vec3 lit = aleph::math::hadamard(base_albedo, amb) * ao + self_emit;
    for (const LoweredEntity& L : lights) {
        const aleph::math::Vec3 d = light_center(L.world_geometry) - point;
        const aleph::math::f32  dist_sq = aleph::math::dot(d, d);
        if (dist_sq < 1e-6f) continue;
        const aleph::math::f32  ndl0 = aleph::math::dot(N, d) / std::sqrt(dist_sq);
        const aleph::math::f32  ndl = two_sided ? std::fabs(ndl0) : (ndl0 > 0.0f ? ndl0 : 0.0f);
        if (ndl <= 0.0f) continue;
        const aleph::math::f32  atten = 1.0f / (1.0f + kFall * dist_sq);
        const aleph::math::f32  vis = light_visibility(point, N, L, occluders, self);
        lit = lit + aleph::math::hadamard(base_albedo, L.material.emit)
                        * (ndl * atten * kLightScale * vis);
    }
    return lit * kRasterExposure;
}
```
**The Lambert branch (`amb`/`lit`/loop/return) is character-for-character the old lines 484-500** — do NOT refactor it. `ao` is still computed once up top (same value). `V` is new but unused in the Lambert path.

- [ ] **Step 4 — thread `mat`+`eye` through the emit chain.** `emit_quad/emit_tri/emit_sphere` currently take `(out, g, albedo, emit, lights, occluders, source, phi)` and call `shade_face(p, n, albedo, emit, lights, two_sided, occluders, source)`. Change each to take `(out, g, const MaterialParams& mat, aleph::math::Vec3 eye, lights, occluders, source, phi)` and call `shade_face(p, n, mat, eye, lights, two_sided, occluders, source)`. `emit_entity` (build_sw.cppm:640) currently unpacks `albedo`/`emit` from `e.material` — change it to take `eye` and forward `e.material` + `eye`: `emit_sphere(out, g, e.material, eye, lights, occluders, e.source, phi)` etc.

- [ ] **Step 5 — `build_sw_scene` eye overload.** Current: `build_sw_scene(const LoweredScene& ls, const std::vector<double>* phi_entity = nullptr)`. Change to TWO functions:
```cpp
// Eye-explicit (the editor passes cam_.look_from()).
[[nodiscard]] inline SwBuild build_sw_scene(const LoweredScene& ls, aleph::math::Vec3 eye,
                                            const std::vector<double>* phi_entity = nullptr) {
    /* the existing body, passing `eye` into each detail::emit_entity(out, ls.entities[i], eye, ls.lights, ls.entities, phi) */
}
// Convenience: legacy callers unchanged — eye defaults to the scene camera.
[[nodiscard]] inline SwBuild build_sw_scene(const LoweredScene& ls,
                                            const std::vector<double>* phi_entity = nullptr) {
    return build_sw_scene(ls, ls.camera.look_from, phi_entity);
}
```
(Confirm `LoweredScene::camera.look_from` exists — `lowered.cppm:89`. `emit_entity`'s signature gains `eye` as in Step 4.)

- [ ] **Step 6 — build + run** `--test-case="build_sw*"`: every LEGACY test compiles unchanged (the convenience overload) and passes (Lambertian entities → the default branch → byte-identical; the `same_face`/AO/sky/shadow oracles hold). Strict 0. **Commit** `feat(sw): material-kind shade_face (Metal/Dielectric) + eye plumbed via overload`.

---

## Task 2: controller rebake_view + shell wiring + visible subjects

**Files:** `bridge/src/aleph.edit/aleph.edit-controller.cppm`, `apps/aleph_edit/main.cpp`.

- [ ] **Step 1 — controller eye + rebake_view.** In `rebuild_backends_from_prev` (controller.cppm:528/530) pass the orbit eye: `build_sw_scene(prev_, cam_.look_from(), &phi_entity)` and `build_sw_scene(prev_, cam_.look_from())`. Add a public method:
```cpp
// Re-bake sw_ (+ render_) at the CURRENT orbit pose. The shell calls this once
// after setting the framed camera, since the ctor baked at the default pose and
// the Metal/Dielectric vcol is view-dependent.
void rebake_view() { rebuild_backends_from_prev(); }
```

- [ ] **Step 2 — shell forces a framed-pose re-bake.** In `apps/aleph_edit/main.cpp`, after the camera pose is set in `run_headless` (~line 331), `run_wave` (~498), and `run_live` (~618) — and BEFORE the first `dump`/render — add `controller.rebake_view();`. (Fixes the wrong-eye-at-construction bake; headless now bakes once at the FINAL pose.)

- [ ] **Step 3 — visible Metal + Dielectric subjects.** In `build_initial_graph` (main.cpp): add a sphere Mesh with `Material{..., MaterialKind::Metal, albedo≈{0.85,0.85,0.9}, fuzz≈0.05}` and a sphere Mesh with `Material{..., MaterialKind::Dielectric, ior=1.5}`, placed beside the existing matte sphere (distinct positions, References to their Materials, Contains from root). Keep the existing Lambertian sphere + floor.

- [ ] **Step 4 — build + run** `--test-case="controller*"` + `--headless` smoke (e.g. `./build-release/apps/aleph_edit/aleph_edit --headless` renders without crash). Confirm no controller test pinned `build_initial_graph`'s entity count breaks (update it if so — the count legitimately grew by 2). Strict 0. **Commit** `feat(edit): rebake_view at framed pose + metal/glass subjects in the editor scene`.

---

## Task 3: material oracles + Lambert golden + artifact

**Files:** `tests/edit/test_build_sw.cpp`.

- [ ] **Step 1 — Lambert byte-identity golden.** Add a namespace-local `shade_lambert_ref(point, N_in, albedo, emit, lights, two_sided, occluders, self)` — a **verbatim copy** of the Lambert branch (normalize N; `ao = detail::ambient_occlusion`; `amb = detail::sky_ambient(N)+detail::sun_tint(...)`; `lit = hadamard(albedo,amb)*ao+emit`; the per-light loop with `detail::light_center`/`detail::light_visibility`/`detail::kFall`/`detail::kLightScale`; `*detail::kRasterExposure`). Build an all-Lambertian scene (`make_shadow_scene` works) and assert every floor/sphere face `vcol[k] == shade_lambert_ref(verts[k], normal_at_k, albedo, emit, lights, two_sided, entities, source)` with **`operator==`** (channelwise, not `Approx`). (The normal per vertex: floor = the quad cross normal; sphere = `vert − center`. Match how `emit_*` calls `shade_face`.) This pins the f32 tree.

- [ ] **Step 2 — Metal env-reflection oracle** (eye `{0,0,5}`, no lights): a Metal sphere (origin r=1) vs a Lambert sphere, same albedo. Iterate front-facing sphere faces (`dot(N,V)>0`, `N=normalize(centroid)`, `V=normalize({0,0,5}-centroid)`); collect metal vs lambert vcol lum. Assert `spread(metal) > spread(lambert)` (max−min) AND the front-facing vertex with min `R.y` (R=reflect(-V,N)) is darker than the one with max `R.y`.

- [ ] **Step 3 — Metal specular lands; fuzz broadens it** (eye `{0,0,5}`): pick a front-facing `on_sphere`-style vertex normal `N0`; `Ldir = N0*(2*dot(V,N0)) − V`; place a light entity centered at `N0_point + Ldir*4`, emit `{3,3,3}`. Build with `fuzz=0`/`fuzz=1`. At the aligned PEAK vertex assert `lum(fuzz0) > lum(no-light env)` (highlight lands) and `lum(fuzz0) == Approx(lum(fuzz1))` (un-normalized `pow(1,p)=1` → peaks equal by construction). At the off-peak NEIGHBOUR vertex assert `lum(fuzz0) < lum(fuzz1)` (sharp lobe falls off faster → broad lobe brighter off-peak). Do NOT assert `peak(fuzz0) > peak(fuzz1)` — unsatisfiable. (Verify `dot(N0, normalize(normalize(center−N0_point)+V)) ≈ 1` as a guard.)

- [ ] **Step 4 — Dielectric rim + dark center** (eye `{0,0,5}`): a Dielectric sphere (origin r=1, neutral albedo ≤1/ch). (a) grazing vertex (min `|dot(N,V)|`) lum > face-on vertex (max `|dot(N,V)|`) lum. (b) build a white Lambert sphere (`albedo={1,1,1}`, no light); the glass face-on vertex (max `|dot(N,V)|`, `|N.y|<0.1`) lum < the white-Lambert face-on vertex lum.

- [ ] **Step 5 — determinism + φ.** `same_face` across two builds of a Metal+Dielectric+Lambert scene (with an eye). The existing φ test still passes (kind branch φ-skipped).

- [ ] **Step 6 — build + run** all `build_sw*` (legacy + 4 new) + full `ctest` + strict 0. Report the metal spread numbers + the dielectric grazing/face-on lums.

- [ ] **Step 7 — artifact.** Before/after (flat-Lambert `main` vs material-kind) of the editor initial scene baked at the framed pose (`rebake_view()`), via headless dump → `docs/superpowers/artifacts/2026-06-06-material-kind-before-after.png` (temp `git worktree` for `main`). Expect chrome (sky-bright top / dark-ground bottom + highlight) + dark-center bright-rim glass beside matte. **Commit** `test(sw): material-kind oracles (Lambert golden, metal, dielectric) + artifact`.

---

## Final verification
- [ ] Lambert golden `==` passes (byte-identical Lambert); metal env-spread + pinned specular + dielectric rim/center oracles pass; `same_face`/φ unchanged.
- [ ] `ctest` all pass; release-strict 0. Headless renders metal+glass without crash, baked at the framed pose.
- [ ] Artifact shows distinct chrome + glass vs matte.
