# Design Spec — Material-Kind Raster Shading (visual slice 4c-i)

**Goal:** make the software-raster preview shade by **MaterialKind** instead of flat Lambert for everything. The path tracer renders Lambertian / **Metal** (reflective) / **Dielectric** (glass, Schlick) / Emissive; the raster bakes the SAME flat Lambert albedo for all, so a chrome ball and a matte ball look identical. This slice gives **Metal** a view-dependent specular highlight + a **signed** environment (sky↔ground) reflection and **Dielectric** a bright Fresnel rim + darkened-center glass wash, so material types read distinctly — closing a real raster↔PT gap. Date 2026-06-06 · Status: REVISED-2 (two adversarial-review passes applied; reordered AFTER polish 4d so the finer 20×28 sphere lets the highlight land; live continuous-orbit tracking split out to follow-up 4c-i-b). First of the materials+texture pair (4c-ii = procedural-texture parity). See [[materials-slice-parked]].

Context (verified): `LoweredEntity::material` (`MaterialParams`) ALREADY carries `kind/albedo/fuzz/ior/emit` (lowering's `to_params` is 1:1); `build_sw`'s `emit_entity` extracts only `albedo`/`emit` and `shade_face` is Lambert-only with **no eye/view vector**. The graph `aleph::types::MaterialKind` has 4 kinds (Lambertian/Metal/Dielectric/Emissive). Spheres are now `kSphereRings=20 × kSphereSectors=28` (4d), vertex spacing 9°×12.9°. The editor raster is viewed through the OrbitCamera `controller.camera()`; `sw_` is baked by `EditorController::rebuild_backends_from_prev()` on `apply(Op)`/wave-`step` (and at construction). Stays in `build_sw` + a thin `EditorController`/shell hook + the editor scene; `render.sw`/`render.rt` untouched.

**Scope of 4c-i (read this):** the material *shading math* + the eye plumbed into the bake, baked at the **framed camera pose** and re-baked on every edit. This is **correct in headless** (the screenshot-review workflow) and in the live editor immediately after any edit/step. **Continuous orbit-without-edit shows a stale highlight** until the next edit — fixing that (a throttled `sw_`-only re-bake on orbit + a view-independent/dependent vcol split so AO/shadows aren't re-cast per frame) is **follow-up slice 4c-i-b** (§7), deliberately split out because it is a perf-sensitive controller/shell sub-system. The headline visual lands now where the user reviews it (headless PNGs).

## 1. Approach

Plumb the **eye** + the full `MaterialParams` down `emit_entity → emit_quad/tri/sphere → shade_face`. `shade_face` gains a `kind` branch using `V = normalize(eye - point)`:

- **Lambertian / Emissive (default):** **BYTE-FOR-BYTE the existing shade** (sky-ambient + AO + contact-shadowed Lambert + `self_emit`) — §3. `V` computed but unused here; MUST NOT perturb the Lambert f32 expression tree.
- **Metal:** body = a **signed** sky↔ground reflection tinted by albedo, plus a tight specular highlight and a small diffuse floor.
  - `R = reflect(-V, N)` (`reflect(v,n)=v−2·dot(v,n)·n`, vec.cppm:83; for `V`=surface→eye, `-V` is the incident ray, head-on `V=N → R=N`). Sample a **signed** environment `env = hadamard(albedo, sky_env(R)) * ao` — `sky_env` keeps R's up/down sign (§2.3) so the chrome reflects bright sky up top, **dark ground** at the bottom (a real vertical gradient). **Do NOT route R through `sky_ambient`** — its world-up fold (4b) would map every downward reflection to bright zenith, erasing the gradient.
  - Per light: **Blinn-Phong** `H = normalize(L + V)`; `spec = pow(max(0, dot(N,H)), shininess)`, `shininess = lerp(kMetalShinSharp, kMetalShinBroad, fuzz)`. Add `hadamard(albedo, light_emit) * spec * atten * kLightScale * vis`. Plus a **small diffuse floor** so the lit side isn't crushed when the highlight misses a vertex: `+ hadamard(albedo, light_emit) * (kMetalDiffuse * ndl * atten * kLightScale * vis)`.
- **Dielectric (glass):** a bright **edge-lit** surface with a **darkened center** (no refraction — a look).
  - **Fresnel** (Schlick, §2.3): `cosV = |dot(N,V)|`; `F = schlick_f32(cosV, ior)`. Grazing `F→1`; face-on `F→r0` (≈0.04 at ior 1.5).
  - Body = `lerp(kGlassCenter, kRimColor, F)`: `kGlassCenter` **dim** (face-on darker than a white Lambert ball — the real discriminator), `kRimColor` bright (`F→1` **brightens** the silhouette; the prior `lerp(kGlassTint, env, F)` *inverted* the rim). Optional faint albedo tint via `kGlassAlbedoMix` applied to `kGlassCenter` only (must not brighten it). AO-multiply the body.
  - Per light: `spec = pow(max(0,dot(N,H)), kGlassShininess) * F`; add `light_emit * spec * atten * kLightScale * vis`.

**φ-skip preserved** (all in `shade_face`). **Gouraud:** specular is per-vertex; at 20×28 (9°×12.9°) + the lowered exponents (§2.4) the lobe spans a vertex cell so the highlight lands; the smooth env-reflection gradient is the primary, vertex-robust metal cue.

## 2. Components

### 2.1 Plumb eye through the bake (overload — no caller churn) + bake at the framed pose
- Add **`build_sw_scene(const LoweredScene& ls, aleph::math::Vec3 eye, const std::vector<double>* phi = nullptr)`** AND keep a **1-/2-arg convenience overload** `build_sw_scene(const LoweredScene& ls, const std::vector<double>* phi = nullptr)` that forwards `build_sw_scene(ls, ls.camera.look_from, phi)`. So **every existing caller (all legacy tests, the controller's phi call) compiles UNCHANGED**; only the editor + the new material tests pass `eye` explicitly. (This is why the review's "~16 broken call sites" do not occur — the overload absorbs them.)
- Thread `eye` → `emit_entity` → `emit_quad/tri/sphere` → `shade_face` (replacing the `albedo, emit` args with `const MaterialParams& mat, Vec3 eye`).
- **`EditorController::rebuild_backends_from_prev()` passes `cam_.look_from()`** (the orbit eye) to the eye-overload — so the per-edit/per-step bake uses the live view.
- **Bake at the framed pose (fixes the wrong-eye-at-construction bug):** the ctor bakes `sw_` while `cam_` is still default `{0,0,5}`; the shell sets the framed pose AFTER construction. Add a public **`EditorController::rebake_view()`** (= `rebuild_backends_from_prev()`), and the shell calls it **once after setting the framed pose** (`run_headless` after main.cpp:~331, `run_wave` ~498, `run_live` ~618) and before the first dump/render. So headless bakes **once at the FINAL pose**. (No throttle / no per-frame view-dirty in 4c-i — that is 4c-i-b.)

### 2.2 `shade_face` signature + kind branch
```cpp
[[nodiscard]] inline aleph::math::Vec3
shade_face(aleph::math::Vec3 point, aleph::math::Vec3 normal,
           const MaterialParams& mat, aleph::math::Vec3 eye,
           const std::vector<LoweredEntity>& lights, bool two_sided,
           const std::vector<LoweredEntity>& occluders, aleph::types::NodeId self) noexcept;
```
Compute unit `N`, `ao`, `V = normalize(eye - point)` (guard `|eye-point|<1e-8 → V=N`). `switch (mat.kind)`: Lambertian/Emissive → the EXACT existing path (`base_albedo=mat.albedo`, `self_emit=mat.emit`); Metal → §1; Dielectric → §1. `self_emit = mat.emit` added in Lambertian/Emissive only (the PT discards emit for Metal/Dielectric, build.cppm:83-86; default emit=0 anyway). Final `* kRasterExposure` unchanged.

### 2.3 Helpers (`build_sw` detail)
**Signed reflection environment** (keeps R.y's sign; continuous at the horizon — both segments give `kSkyHorizon` at `a=0.5`):
```cpp
[[nodiscard]] inline aleph::math::Vec3 sky_env(aleph::math::Vec3 R) noexcept {
    const aleph::math::f32 a = std::clamp(0.5f * R.y + 0.5f, 0.0f, 1.0f);  // R.y in [-1,1]
    return (a < 0.5f) ? aleph::math::lerp(kSkyGround, kSkyHorizon, a * 2.0f)        // R.y<0: ground..horizon
                      : aleph::math::lerp(kSkyHorizon, kSkyZenith, (a - 0.5f) * 2.0f); // R.y>0: horizon..zenith
}
```
**Schlick — MANUAL quintic** (NOT `std::pow`; reproducibility — `std::pow` is not bit-stable across libm and would threaten cross-machine artifacts; mirrors render.rt-material.cppm:34-40):
```cpp
[[nodiscard]] inline aleph::math::f32 schlick_f32(aleph::math::f32 cosine, aleph::math::f32 ior) noexcept {
    aleph::math::f32 r0 = (1.0f - ior) / (1.0f + ior); r0 = r0 * r0;
    const aleph::math::f32 oc = std::max(0.0f, 1.0f - cosine);
    return r0 + (1.0f - r0) * oc * oc * oc * oc * oc;
}
```
`reflect` (vec.cppm:83) reused; `std::clamp(..., 0.0f, 1.0f)` with float literals (aleph.math has no `clamp`; matches build_sw.cppm:121/252). The Blinn-Phong `std::pow(dot(N,H), shininess)` (variable exponent) is the ONE libm-dependent term — affects only Metal/Dielectric vcol (relational §5 oracles); Lambert stays `std::pow`-free + byte-exact; run-to-run `same_face` holds (process-deterministic). (No `-Wconversion`/`-Wdouble-promotion` gate exists in this build — only `-Wsign-conversion`; the helpers introduce no signed↔unsigned narrowing.)

### 2.4 Constants (near the sky/sun constants)
```cpp
inline constexpr aleph::math::Vec3 kSkyGround    = {0.10f, 0.10f, 0.12f}; // reflected ground (dark)
inline constexpr aleph::math::f32  kMetalShinSharp = 16.0f;  // fuzz=0 (lobe ~ a 20x28 vertex cell)
inline constexpr aleph::math::f32  kMetalShinBroad =  4.0f;  // fuzz=1
inline constexpr aleph::math::f32  kMetalDiffuse   = 0.15f;  // small diffuse floor (metal not crushed dark)
inline constexpr aleph::math::f32  kGlassShininess = 24.0f;  // glass highlight (lands on the finer sphere)
inline constexpr aleph::math::Vec3 kGlassCenter  = {0.22f, 0.25f, 0.30f}; // DIM center (sum 0.77 << white-Lambert face-on ~0.86+)
inline constexpr aleph::math::Vec3 kRimColor     = {1.30f, 1.35f, 1.45f}; // BRIGHT Fresnel rim (> any sky channel)
inline constexpr aleph::math::f32  kGlassAlbedoMix = 0.15f;  // faint albedo tint on the center only
```
(`kSkyZenith`/`kSkyHorizon` from 4b, `kLightScale`/`kRasterExposure`, `sky_ambient`/`sun_tint` reused for the Lambert branch; Metal/Dielectric use `sky_env(R)`/the glass body instead.)

### 2.5 Visible subjects in the editor scene
`build_initial_graph` (apps/aleph_edit/main.cpp) makes only Lambertian spheres + floor. Add a **Metal** sphere (`MaterialKind::Metal`, an albedo tint, small `fuzz`) and a **Dielectric** sphere (`MaterialKind::Dielectric`, `ior≈1.5`) so the editor preview + the before/after artifact show chrome + glass next to matte. (Note: adding entities shifts entity/pick indices — check no other test pins `build_initial_graph`'s entity count; the headless `step1_add_object` artifact re-renders fine.) Lattice/headless `AddObject` stay Lambertian.

## 3. Determinism
`eye` is fixed per bake (the orbit pose at bake time). `reflect`/`sky_env`/Schlick(manual)/`sun_tint` are pure-f32; the kind branch is a `switch` on a fixed enum. Same `(LoweredScene, eye)` ⇒ byte-identical `vcol`; `same_face` holds run-to-run. The ONE libm-dependent term is the Metal/Dielectric Blinn-Phong `std::pow` (relational oracles). `--wave`/headless byte-identity unaffected (wave φ-skips shade). **Lambert byte-identity is enforced by a GOLDEN (not Approx):** the Lambertian/Emissive branch must reproduce the existing `shade_face` expression tree exactly; §5 pins it with `shade_lambert_ref` + `operator==`.

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions. `V` guarded for `eye≈point` (→N). `H=normalize(L+V)` guarded (`|L+V|<1e-8` → skip the highlight). `pow(neg,·)` avoided by `max(0,dot)`. Degenerate `N` → existing `(0,1,0)` fallback. Unknown/future `kind` → default (Lambertian) branch.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
**Legacy tests compile UNCHANGED** (the §2.1 convenience overload). New material oracles use the eye-overload. **Normative precondition (material oracles):** set `ls.camera.look_from` **outside the sphere by ≥2·radius** (sphere at origin r=1, eye `{0,0,5}`) and pass it as `eye`; the `V=N` guard must NOT be the operative path.

- **Lambertian byte-identity (GOLDEN, ==):** add a free function `shade_lambert_ref(point, N, albedo, emit, lights, two_sided, occluders, self)` in the test file — a **verbatim copy of the current (pre-change) Lambert shade body** (`amb=sky_ambient(N)+sun_tint(...)`; `lit=hadamard(albedo,amb)*ao+emit`; per-light `hadamard(albedo,emit)*(ndl*atten*kLightScale*vis)`; `*kRasterExposure`). For an all-Lambertian scene assert each face `vcol[k] == shade_lambert_ref(...)` channelwise with **`operator==`** (not `Approx`). This is the only construction that pins the f32 tree byte-for-byte (the existing Approx/relational oracles cannot). Keep `same_face` (determinism) too.
- **Metal env-reflection (vertex-robust, lights-free):** a Metal sphere vs a Lambert sphere (same albedo), NO lights, eye `{0,0,5}`. Among **front-facing** vertices (`dot(N,V)>0`): assert (a) the metal's per-vertex luminance **spread** (max−min) **exceeds** the Lambert sphere's, and (b) the front-facing vertex with the most **downward** reflection (min `R.y`, which exists — a bottom-front vertex gives `R.y≈−0.97`, verified) is **darker** than the one with the most **upward** reflection (max `R.y`) — proving the SIGNED ground term (under the folded `sky_ambient` they'd be equal). Smooth across vertices, no specular-spike dependence.
- **Metal specular lands; fuzz broadens it:** pick a real `on_sphere(ring,sector)` outward normal `N0` (front-facing); with eye `{0,0,5}` compute `Ldir = 2·dot(V,N0)·N0 − V` so `H=normalize(Ldir+V)=N0`. **Realize it by placing the light entity's center at `N0_point + t·Ldir` (t≈4)** — `shade_face` derives `L=normalize(light_center−point)` from the vertex POINT, not a direction; verify `dot(N0, normalize(normalize(light_center−N0_point)+V)) ≈ 1`. The highlight LANDS: assert the aligned vertex's luminance with `fuzz=0` > its no-light env body. **Note (un-normalized Blinn-Phong):** at the exact peak `ndh=1`, so `pow(1, shininess)=1` for ANY exponent → the fuzz0/fuzz1 PEAKS are EQUAL by construction (`lum(fuzz0) == Approx(lum(fuzz1))`); the fuzz→sharpness property must therefore be tested OFF the peak: assert at the off-peak NEIGHBOUR vertex `lum(fuzz=0) < lum(fuzz=1)` (the sharp lobe falls off faster → the broad lobe is brighter off-peak = a broader highlight). (Do NOT assert `peak(fuzz0) > peak(fuzz1)` — mathematically unsatisfiable for an un-normalized lobe.)
- **Dielectric Fresnel rim + dark center:** a Dielectric sphere, eye `{0,0,5}`. (a) the **grazing** vertex (min `|dot(N,V)|`, `F→1→kRimColor`) is **brighter** than the **face-on** vertex (max `|dot(N,V)|`, `F→r0→kGlassCenter`) — rock-solid (`kRimColor` sum 4.1 ≫ `kGlassCenter` sum 0.77, same `*ao*exposure`). (b) the **center** discriminator: compare **luminance (x+y+z)** of the glass face-on vertex (picked as max`|dot(N,V)|` AND `|N.y|<0.1`, the horizon — worst case) against a **white Lambert** sphere (`albedo == {1,1,1}` exactly, no light) face-on vertex; assert glass center is **darker** (with `kGlassCenter` sum 0.77, glass face-on ≈0.65·exposure ≈0.55 vs white-Lambert horizon ≈0.86·exposure — comfortable margin).
- **Determinism:** `same_face` across two builds for a Metal+Dielectric+Lambert scene (run-to-run; `std::pow` is process-deterministic).
- **φ-skip:** the existing φ test still passes.
- **Visual:** before/after (flat-Lambert vs material-kind) of the editor initial scene (with metal + glass spheres), baked at the framed pose (call `rebake_view()` after the pose set) → `docs/superpowers/artifacts/2026-06-06-material-kind-before-after.png`. Expect a chrome ball (sky-bright top / dark-ground bottom + sharp highlight) and a dark-centered bright-rimmed glass ball, beside the matte ones.

## 6. Cost / when it runs
Per vertex: one `normalize` (V) + per light a `reflect`/`pow`/Schlick for Metal/Dielectric. The bake itself (AO `kAoRays×occluders` + per-light `light_visibility` `kShadowSamples²×occluders` per vertex) is the dominant cost (~hundreds of K segment tests for the editor scene, single-threaded) — but in 4c-i it runs **per-edit + once at the framed pose**, NOT per orbit frame. `--wave` φ-skips shade entirely (lattice pays nothing). So 4c-i adds no per-frame cost. (The per-orbit re-bake — and the optimization to make it cheap — is 4c-i-b, §7.)

## 7. Scope boundary (YAGNI)
**In (4c-i):** view-dependent Metal (signed sky↔ground reflection + Blinn-Phong + diffuse floor) and Dielectric (Schlick Fresnel bright rim + dim center + sharp highlight) raster shading via the present `kind/fuzz/ior` + the plumbed eye (overload); bake at the framed pose + on every edit; the Lambert golden; visible metal/glass subjects. **Out:**
- **Follow-up slice 4c-i-b — live continuous-orbit view tracking:** a throttled (`kViewRebakeMs≈80`, shell-side so the controller stays deterministic) **`sw_`-only** re-bake on orbit/zoom (set a `view_dirty` bool after the main.cpp orbit (~702) + zoom (~707) mutations; re-bake in the render block when dirty + interval elapsed; one re-bake on the idle→path-trace transition), plus a **view-independent/dependent vcol split** so AO/shadows/diffuse (eye-independent) are cached and only the Metal/Dielectric term recomputes on orbit, and the re-bake rebuilds ONLY `sw_` (NOT the PT `render_`/BVH, which is view-independent + only used when idle — `rebuild_backends_from_prev` currently rebuilds both). **Limitation in 4c-i until then:** continuous orbit-without-edit shows a stale highlight; headless/screenshot review + post-edit live view are correct.
- *Procedural texture / `TexturedLambertian`* — slice **4c-ii**.
- *True mirror reflection / refraction / glass compositing* — the raster reflects only the analytic `sky_env` ramp, not scene geometry; the PT is the truth.
- *Per-pixel (Phong) specular*, *drawing the area-light quads* — out.
All new constants are `constexpr` tunables.
