# Design Spec — Material-Kind Raster Shading (visual slice 4c-i)

**Goal:** make the software-raster preview shade by **MaterialKind** instead of flat Lambert for everything. The path tracer renders Lambertian / **Metal** (reflective) / **Dielectric** (glass, Schlick) / Emissive; the raster bakes the SAME flat Lambert albedo for all, so a chrome ball and a matte ball look identical. This slice gives **Metal** a view-dependent specular highlight + a **signed** environment (sky↔ground) reflection and **Dielectric** a bright Fresnel rim + darkened-center glass wash, so material types read distinctly — closing a real raster↔PT gap. Date 2026-06-06 · Status: REVISED (adversarial review applied; reordered AFTER polish 4d so the finer 20×28 sphere lets the highlight land). First of the materials+texture pair (4c-ii = procedural-texture parity). See [[materials-slice-parked]] for the review trail.

Context (verified): `LoweredEntity::material` (`MaterialParams`) ALREADY carries `kind/albedo/fuzz/ior/emit` (lowering's `to_params` is 1:1); `build_sw`'s `emit_entity` extracts only `albedo`/`emit` and `shade_face` is Lambert-only with **no eye/view vector**. The graph `aleph::types::MaterialKind` has 4 kinds (Lambertian/Metal/Dielectric/Emissive). Spheres are now `kSphereRings=20 × kSphereSectors=28` (slice 4d), vertex spacing 9°×12.9°. **The editor raster is viewed through the OrbitCamera `controller.camera()`, NOT the static graph camera** — `sw_` is baked by `EditorController::rebuild_backends_from_prev()` (only on `apply(Op)`/wave-`step`), and `prev_.camera` is the one-time graph Camera; `cam_` (orbit) is separate. So a view-dependent bake MUST source the eye from `cam_` and re-bake when it moves. Stays in `build_sw` + `EditorController` + the editor scene; `render.sw`/`render.rt` untouched.

## 1. Approach

Plumb the **eye** + the full `MaterialParams` down the `emit_entity → emit_quad/tri/sphere → shade_face` chain. `shade_face` gains a `kind` branch using `V = normalize(eye - point)`:

- **Lambertian / Emissive (default):** **BYTE-FOR-BYTE the existing shade** (sky-ambient + AO + contact-shadowed Lambert + `self_emit`) — see §3. `V` is computed but unused here and MUST NOT perturb the Lambert f32 expression tree.
- **Metal:** body = a **signed** sky↔ground reflection tinted by albedo, plus a tight specular highlight.
  - `R = reflect(-V, N)` (mirror of the incident view about N; `reflect` = `v − 2·dot(v,n)·n`, vec.cppm:83). Sample a **signed** environment `env = hadamard(albedo, sky_env(R)) * ao` — `sky_env` keeps R's up/down sign (§2.3) so the chrome ball reflects bright sky up top and the **dark ground** at the bottom (a real vertical gradient). **Do NOT route R through `sky_ambient`** — its unconditional world-up fold (slice 4b) would map every downward reflection back to bright zenith, erasing the gradient. AO still occludes the body.
  - Per light: **Blinn-Phong** `H = normalize(L + V)`; `spec = pow(max(0, dot(N,H)), shininess)`, `shininess = lerp(kMetalShinSharp, kMetalShinBroad, fuzz)` (sharp at fuzz≈0). Add `hadamard(albedo, light_emit) * spec * atten * kLightScale * vis`. Metal **keeps a small broad diffuse floor** (`kMetalDiffuse`, §2.4) so the lit side is not crushed to near-black when the highlight misses a vertex (the review's "metal reads dark" guard): `+ hadamard(albedo, light_emit) * (kMetalDiffuse * ndl * atten * kLightScale * vis)`.
- **Dielectric (glass):** a bright **edge-lit** surface with a **darkened center** (no refraction/compositing — a look, not transmission).
  - **Fresnel** (Schlick, §2.3): `cosV = |dot(N,V)|`; `F = schlick_f32(cosV, ior)`. Grazing (`cosV→0`) `F→1`; face-on `F→r0` (≈0.04 at ior 1.5).
  - Body = `lerp(kGlassCenter, kRimColor, F)` where `kGlassCenter` is a **dim** center tint (so face-on is darker than a white Lambert ball — the real discriminator) and `kRimColor ≥` every channel of a bright rim, so `F→1` **brightens** the silhouette (the review proved the old `lerp(kGlassTint, env, F)` *inverted* the rim — grazing came out darker). Faint albedo tint via `kGlassAlbedoMix`. AO-multiply the body.
  - Per light: sharp highlight `spec = pow(max(0,dot(N,H)), kGlassShininess) * F`; add `light_emit * spec * atten * kLightScale * vis`.

**φ-skip preserved** (all in `shade_face`, bypassed by `phi ? fc : shade_face(...)`). **Gouraud caveat:** specular is per-vertex; at the new 20×28 sphere (9°×12.9°) and the **lowered** exponents (§2.4) the lobe spans a vertex cell so the highlight lands (the review showed 96/128 on a 12×16 sphere was sub-vertex — fixed by 4d + lower exponents). The smooth env-reflection gradient is the primary, vertex-robust metal cue.

## 2. Components

### 2.1 Plumb eye through the bake + re-bake on orbit
- `build_sw_scene(const LoweredScene& ls, aleph::math::Vec3 eye, const std::vector<double>* phi = nullptr)` — **add `eye` as a required 2nd param** (existing callers updated; the natural default for non-editor callers is `ls.camera.look_from`, but pass it explicitly). Thread `eye` → `emit_entity` → `emit_quad/tri/sphere` → `shade_face` (replacing the `albedo, emit` args with `const MaterialParams& mat, Vec3 eye`).
- **`EditorController::rebuild_backends_from_prev()` passes `cam_.look_from()`** (the ORBIT eye), not `prev_.camera.look_from` — so every bake uses the live view.
- **Debounced orbit re-bake:** the editor view is currently re-baked only on `apply(Op)`/`step`. Add a controller method `note_view_changed()` that marks `sw_` view-dirty; the shell calls it after an orbit/zoom gesture. The render path re-bakes when view-dirty AND a throttle interval (`kViewRebakeMs ≈ 80`) has elapsed (and once on gesture-end), so continuous orbit re-bakes at ~12 Hz, not every frame. Headless (`run_headless`) sets a fixed `cam_` and bakes once — no throttle needed. (Cost + the view-independent/dependent split hook: §6/§7.)

### 2.2 `shade_face` signature + kind branch
```cpp
[[nodiscard]] inline aleph::math::Vec3
shade_face(aleph::math::Vec3 point, aleph::math::Vec3 normal,
           const MaterialParams& mat, aleph::math::Vec3 eye,
           const std::vector<LoweredEntity>& lights, bool two_sided,
           const std::vector<LoweredEntity>& occluders, aleph::types::NodeId self) noexcept;
```
Compute unit `N`, `ao` (existing), `V = normalize(eye - point)` (guard `|eye-point|<1e-8 → V = N`). `switch (mat.kind)`: Lambertian/Emissive → the exact existing path (`base_albedo=mat.albedo`, `self_emit=mat.emit`); Metal → §1; Dielectric → §1. `self_emit = mat.emit` added in Lambertian/Emissive only — NOT Metal/Dielectric (the PT discards emit for those kinds, build.cppm:83-86; adding it raster-only manufactures a divergence; default emit=0 anyway). Final `* kRasterExposure` unchanged.

### 2.3 Helpers (`build_sw` detail)
**Signed reflection environment** (keeps R.y's sign, unlike the world-up-folding `sky_ambient`):
```cpp
[[nodiscard]] inline aleph::math::Vec3 sky_env(aleph::math::Vec3 R) noexcept {
    const aleph::math::f32 a = std::clamp(0.5f * R.y + 0.5f, 0.0f, 1.0f);  // R.y in [-1,1]
    // ground (down) -> horizon -> zenith (up); two-segment ramp through the horizon.
    return (a < 0.5f) ? aleph::math::lerp(kSkyGround, kSkyHorizon, a * 2.0f)
                      : aleph::math::lerp(kSkyHorizon, kSkyZenith, (a - 0.5f) * 2.0f);
}
```
**Schlick — MANUAL quintic** (NOT `std::pow`, which is not bit-reproducible → would break `same_face`; mirrors render.rt-material.cppm:34-40):
```cpp
[[nodiscard]] inline aleph::math::f32 schlick_f32(aleph::math::f32 cosine, aleph::math::f32 ior) noexcept {
    aleph::math::f32 r0 = (1.0f - ior) / (1.0f + ior); r0 = r0 * r0;
    const aleph::math::f32 oc = std::max(0.0f, 1.0f - cosine);
    return r0 + (1.0f - r0) * oc * oc * oc * oc * oc;
}
```
`reflect` (vec.cppm:83) is reused. The Blinn-Phong `std::pow(dot(N,H), shininess)` is the ONE libm-dependent term (variable exponent can't be unrolled) — it only affects Metal/Dielectric vcol, whose §5 oracles are relational; Lambert vcol stays `std::pow`-free and byte-exact. (Run-to-run `same_face` still holds: `std::pow` is deterministic for fixed inputs within a process.)

### 2.4 Constants (near the sky/sun constants)
```cpp
inline constexpr aleph::math::Vec3 kSkyGround    = {0.10f, 0.10f, 0.12f}; // reflected ground (dark)
inline constexpr aleph::math::f32  kMetalShinSharp = 16.0f;  // fuzz=0 (lobe ~ a 20x28 vertex cell)
inline constexpr aleph::math::f32  kMetalShinBroad =  4.0f;  // fuzz=1
inline constexpr aleph::math::f32  kMetalDiffuse   = 0.15f;  // small diffuse floor so metal is not crushed
inline constexpr aleph::math::f32  kGlassShininess = 24.0f;  // glass highlight (lands on the finer sphere)
inline constexpr aleph::math::Vec3 kGlassCenter  = {0.30f, 0.34f, 0.40f}; // DIM center (darker than white Lambert)
inline constexpr aleph::math::Vec3 kRimColor     = {1.30f, 1.35f, 1.45f}; // BRIGHT Fresnel rim (> any sky channel)
inline constexpr aleph::math::f32  kGlassAlbedoMix = 0.15f;  // faint albedo tint on glass
```
(`kSkyZenith`/`kSkyHorizon` from 4b, `kLightScale`/`kRasterExposure`, `sky_ambient`/`sun_tint` reused. The current ambient seed `sky_ambient(N)+sun_tint(...)` is the Lambertian/Emissive branch; Metal/Dielectric use `sky_env(R)`/the glass body instead.)

### 2.5 Visible subjects in the editor scene
`build_initial_graph` (apps/aleph_edit/main.cpp) makes only Lambertian spheres + floor. Add a **Metal** sphere (`MaterialKind::Metal`, an albedo tint, small `fuzz`) and a **Dielectric** sphere (`MaterialKind::Dielectric`, `ior≈1.5`) so the editor preview + the before/after artifact show chrome + glass next to matte. Lattice/headless `AddObject` stay Lambertian. (Headless `step1_add_object` may add a metal subject so the artifact exhibits it.)

## 3. Determinism
`eye` is fixed per bake (the orbit pose at bake time). `reflect`/`sky_env`/Schlick (manual quintic)/`sun_tint` are pure-f32; the kind branch is a `switch` on a fixed enum; lights in order. Same `(LoweredScene, eye)` ⇒ byte-identical `vcol`; `same_face` holds run-to-run. **Lambertian byte-identity is NORMATIVE:** the Lambertian/Emissive branch MUST reproduce the existing `shade_face` f32 expression tree, operand grouping, and light order exactly (`amb = sky_ambient(N)+sun_tint(...)`; `lit = hadamard(albedo,amb)*ao + emit`; per-light `hadamard(albedo,emit)*(ndl*atten*kLightScale*vis)`); computing `V` and the `switch` must not alter these ops. The ONE libm-dependent term is the Metal/Dielectric Blinn-Phong `std::pow` (relational oracles, §5). `--wave`/headless byte-identity unaffected (wave φ-skips shade).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions. `V` guarded for `eye≈point` (→ N). `H = normalize(L+V)` guarded (`|L+V|<1e-8` → skip the highlight). `pow(neg,·)` avoided by `max(0, dot)`. Degenerate `N` → existing `(0,1,0)` fallback. Unknown/future `kind` → default (Lambertian) branch.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
**Normative precondition (all material oracles):** the test `LoweredScene` MUST set `ls.camera.look_from` **outside the sphere by ≥ 2·radius** (e.g. sphere at origin r=1, eye `{0,0,5}`) and pass it as the `eye` arg, so `V` is a genuine view direction spanning `|dot(N,V)|` from ~0 (silhouette) to ~1 (front pole); the `V=N` degeneracy guard must NOT be the operative path.

- **Lambertian byte-identity (regression, ==):** build an all-Lambertian scene; assert each face's `vcol` is **byte-identical** to a baseline captured from the pre-change shade. Concretely: the existing AO/sky/contact-shadow oracles' exact values MUST be unchanged, AND `same_face` holds. (Proves the refactor + the `V`/switch didn't perturb Lambert.)
- **Metal env-reflection (vertex-robust, lights-free):** a Metal sphere vs a Lambert sphere (same albedo), **no lights**, eye `{0,0,5}`. Metal's body is `hadamard(albedo, sky_env(R))*ao` — a **signed** vertical gradient; assert the metal's per-vertex luminance **spread** (max−min over the visible hemisphere) **exceeds** the Lambert sphere's (whose ambient is the gentler `sky_ambient(N)`), AND a downward-reflecting vertex (R.y<0) is **darker** than an upward-reflecting one (proving the signed ground term — would be equal under the folded `sky_ambient`). This is smooth across vertices (no specular-spike dependence).
- **Metal specular lands on a pinned vertex:** place the light + eye so that for a KNOWN sphere grid-vertex normal `N₀`, `H = normalize(L+V) ≈ N₀` (compute L from the desired H); assert that vertex's luminance with `fuzz=0` exceeds its luminance with `fuzz=1` (sharp lobe peaks higher AT the aligned vertex) and exceeds the same vertex's no-light env body. (Pins the lobe to a vertex — robust at the new resolution; not a generic "brightest vertex" claim.)
- **Dielectric Fresnel rim + dark center (corrected magnitudes):** a Dielectric sphere, eye `{0,0,5}`. (a) a **grazing** vertex (min `|dot(N,V)|`, `F→1` → `kRimColor`) is **brighter** than a **face-on** vertex (max `|dot(N,V)|`, `F→r0` → `kGlassCenter`) — provably true since `kRimColor` (sum ≈4.1) > `kGlassCenter` (sum ≈1.04). (b) the face-on (center) vertex is **darker** than a white Lambert sphere's face-on vertex (the real glass discriminator). Pick vertices by `|dot(N,V)|` using the test `eye`.
- **Determinism:** `same_face` across two builds for a Metal+Dielectric+Lambert scene (run-to-run; `std::pow` is process-deterministic).
- **φ-skip:** the existing φ test still passes (kind branch bypassed in wave mode).
- **Visual:** before/after (flat-Lambert vs material-kind) of the editor initial scene (with a metal + glass sphere), eye = the editor orbit pose → `docs/superpowers/artifacts/2026-06-06-material-kind-before-after.png`. Expect a chrome ball with a sky-bright top / dark-ground bottom + a sharp highlight, and a dark-centered bright-rimmed glass ball, beside the matte ones.

## 6. Cost / when it runs
Per vertex: one `normalize` (V) + per light a `reflect`/`pow`/Schlick for Metal/Dielectric — `O(verts × lights)`, dwarfed by AO's ray casts. The new cost is the **orbit re-bake**: `rebuild_backends_from_prev` now also fires on view change (debounced to `kViewRebakeMs≈80`), re-running the FULL bake (incl. view-independent AO/shadows) at the new eye. At the editor scene size (few entities, 20×28 spheres) a bake is a few ms → ~12 Hz orbit re-bake is acceptable. `--wave` φ-skips shade, so the lattice pays nothing. **§7 hook:** cache the view-independent vcol (ambient+AO+diffuse+shadows) and recompute only the view-dependent Metal/Dielectric term on orbit (no AO re-cast) if the debounced full re-bake is too heavy.

## 7. Scope boundary (YAGNI)
**In:** view-dependent Metal (signed sky↔ground reflection + Blinn-Phong + small diffuse floor) and Dielectric (Schlick Fresnel bright rim + dim center + sharp highlight) raster shading via the present `kind/fuzz/ior` + the plumbed orbit eye; debounced orbit re-bake; visible metal/glass subjects. **Out (hooks kept):**
- *Procedural texture / `TexturedLambertian`* — slice **4c-ii**.
- *View-independent/dependent bake split* — recompute only the material term on orbit (avoid re-AO); the §6 debounce makes the full re-bake acceptable at editor sizes for now.
- *True mirror reflection / refraction / glass compositing* — the raster reflects only the analytic sky↔ground ramp (`sky_env`), not scene geometry; glass does not refract the background. The PT is the truth.
- *Per-pixel (Phong) specular* — Gouraud per-vertex is the model; 4d's finer spheres make it land.
- *Drawing the area-light quads in the raster* — pre-existing gap, unrelated.
All new constants are `constexpr` tunables.
