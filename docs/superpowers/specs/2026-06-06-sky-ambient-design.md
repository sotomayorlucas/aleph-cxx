# Design Spec — Directional Sky Ambient + Sun Tint (visual slice 4b)

**Goal:** replace the raster preview's flat grey ambient (`kAmbient = 0.45`, direction-independent) with a **directional ambient** = a hemispheric **sky** term (cool from above, neutral at the horizon, sampled by the surface normal) **plus** a soft warm **sun tint** wrapped from the dominant light's direction. The ambient stops being uniform grey and starts reading like environment light — up-faces pick up the sky, the lit side gets a warm fill past the terminator — so the preview looks "rendered". Date 2026-06-06 · Status: DRAFT. Follows visual slice 4a (per-vertex AO).

Context: the **visible background sky already matches** between path-trace (`render.common::sky_sample` on ray-miss, by `dir.y`) and raster (`clear_sky` screen-`y` gradient, same `kSky`). That gap is closed. The remaining gap is the **ambient term** in the bake: `bridge/.../aleph.lowering-build_sw.cppm`'s `shade_face` seeds `lit = base_albedo*(kAmbient*ao) + self_emit` with a single grey scalar. This slice makes that seed directional + colored. Stays entirely in `build_sw` (the bridge); `render.sw` stays graph-free; the path tracer is untouched.

## 1. Approach

Per shaded vertex with unit normal `N`, the ambient color becomes:

```
ambient(N, lights) = sky_ambient(N) + sun_tint(N, L_sun)
lit                = base_albedo * (ambient(N, lights) * ao) + self_emit
```

- **`sky_ambient(N)`** — a hemispheric gradient sampled by the **world-up-reoriented** normal (the floor normal points DOWN — `cross(u,v) = (0,-64,0)` — so reuse the AO world-up flip `N_up = dot(N,{0,1,0})<0 ? -N : N`). `a = clamp(N_up.y, 0, 1)`; `sky_ambient = lerp(kSkyHorizon, kSkyZenith, a)`. A floor (N_up.y≈1) gets the bright cool zenith; a vertical wall (N_up.y≈0) gets the dimmer horizon. The constants' luminance is calibrated so the **average ambient ≈ the old 0.45 grey** (no global brightness jump).
- **`sun_tint(N, L_sun)`** — a soft warm fill from the dominant light, a cheap half-bounce fake. `L_sun = normalize(center(primary_light) - point)`; wrap factor `w = max(0, 0.5*dot(N,L_sun) + 0.5)` (Valve half-Lambert — tints slightly *past* the terminator, like sky/ground bounce off the lit side); `sun_tint = kSunStrength * w * kSunColor`. Warm (`kSunColor` reddish). Kept small so it's a fill, not a second key light. No primary light ⇒ `sun_tint = 0`.
- Both terms live INSIDE the **AO-multiplied** ambient (so AO occludes the fill too — the pooling under the sphere darkens the whole ambient, not just the sky half). The **direct** light path (`base_albedo·emit·ndl·atten·kLightScale·vis`) and the final `* kRasterExposure` are **UNCHANGED** — this slice only restructures the ambient seed.

**Deterministic** (normal + light geometry + fixed `constexpr` constants; no camera, no random). **φ-skip preserved** (the ambient lives in `shade_face`, which the `phi ? fc : shade_face(...)` fast path bypasses → wave mode unaffected). **No new params / no plumbing:** `shade_face` already receives `lights`; the sky constants are local to `build_sw` `detail` exactly like `kAmbient`/`kRasterExposure`/`kLightScale`.

## 2. Components (all in `build_sw` `detail`)

### 2.1 Constants (near `kAmbient`)
```cpp
// Hemispheric sky ambient — luminance calibrated so the average over the
// hemisphere ≈ the prior flat kAmbient (0.45), now cool-tinted + directional.
inline constexpr aleph::math::Vec3 kSkyZenith  = {0.43f, 0.48f, 0.56f}; // up-facing (cool, bright)
inline constexpr aleph::math::Vec3 kSkyHorizon = {0.38f, 0.39f, 0.42f}; // side-facing (neutral, dimmer)
// Soft warm half-bounce fill from the dominant light (fake sun GI). Small.
inline constexpr aleph::math::Vec3 kSunColor    = {0.55f, 0.42f, 0.28f}; // warm tint direction
inline constexpr aleph::math::f32  kSunStrength = 0.12f;                 // fill weight (<< key light)
```
`kAmbient` (0.45) is **removed** from the ambient seed (the sky constants carry both tint and magnitude). It may remain referenced by tests as a documentation anchor but is no longer in the shading path; delete the unused `inline constexpr` if nothing references it after the test update (avoid an `-Wunused` / dead-constant smell — confirm with grep).

### 2.2 `sky_ambient(N) -> Vec3`
```cpp
[[nodiscard]] inline aleph::math::Vec3 sky_ambient(aleph::math::Vec3 N) noexcept;
```
World-up flip (`N_up = dot(N,{0,1,0})<0 ? N*-1 : N`), `a = clamp(N_up.y, 0, 1)`, `lerp(kSkyHorizon, kSkyZenith, a)`. Reuses `aleph::math::lerp`. (Mirrors `render.common::sky_sample`'s mapping but hemispheric: `a = N_up.y` not `0.5*(y+1)`, since after the flip `N_up.y ∈ [0,1]`.)

### 2.3 `sun_tint(point, N, lights) -> Vec3`
```cpp
[[nodiscard]] inline aleph::math::Vec3
sun_tint(aleph::math::Vec3 point, aleph::math::Vec3 N,
         const std::vector<LoweredEntity>& lights) noexcept;
```
Pick the **primary light** = the light in `lights` with the greatest emissive luminance (`emit.x+emit.y+emit.z`); tie-break by lowest index (deterministic). If `lights` empty ⇒ return `{0,0,0}`. Else `L = normalize(light_center(primary.geom) - point)` (reuse the existing `light_center`); `w = max(0, 0.5f*dot(N,L) + 0.5f)`; return `kSunStrength * w * kSunColor`. Uses the RAW `N` (the sun is a real direction; no world-up flip — a down-facing floor toward an overhead light correctly gets `dot<0` → wrapped small, which is fine).

### 2.4 `shade_face` ambient seed
Change the seed line from
```cpp
aleph::math::Vec3 lit = base_albedo * (kAmbient * ao) + self_emit;
```
to
```cpp
const aleph::math::Vec3 amb = sky_ambient(N) + sun_tint(point, N, lights);
aleph::math::Vec3 lit = aleph::math::hadamard(base_albedo, amb) * ao + self_emit;
```
**CRITICAL — use `hadamard`, not `*`.** In `aleph.math`, `operator*(Vec3,Vec3)` is the **geometric product** (→ `Multivector`, in `:multivector`), NOT component-wise. Per-channel albedo×color MUST use `aleph::math::hadamard(a, b)` — exactly the idiom the existing direct-light line uses (`lit + hadamard(base_albedo, L.material.emit) * (...)`, build_sw.cppm:450). `* ao` then is the f32 scale (`operator*(f32)`). `ao` (slice 4a) and `N` (unit, normalized in `shade_face`) already exist; `point`/`lights` are already params. Everything downstream (direct light × `vis`, atten, `* kRasterExposure`) unchanged.

## 3. Determinism
Fixed `constexpr` constants; `sky_ambient`/`sun_tint` are pure-f32 of `N`/light geometry; primary-light pick is max-luminance with index tie-break; `lights` iterated in order. Same `LoweredScene` ⇒ byte-identical `vcol`. `test_build_sw`'s `same_face` byte-equality oracle holds. `--wave`/`--headless` byte-identity unaffected (wave skips shade). NOTE: this changes the **deterministic raster baseline** vs slice 4a (the ambient model changed); any committed raster PPM baseline must be re-blessed — there is none gating today (only the before/after PNG artifact, not a test).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions. Degenerate `N` (`|N|≈0`) → `shade_face`'s existing fallback `N=(0,1,0)` → `sky_ambient` = zenith, `sun_tint` finite. Zero-length `L` (light exactly at the point) → `normalize` divides by ~0; guard: if `|center-point| < eps` treat `w` as 0 (no tint) — degenerate, never in practice, but no NaN.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
Oracles assert **relationships**, not magic magnitudes, so constant tuning doesn't break them.

- **Hemispheric (sky):** one scene, NO lights (isolates the sky ambient; `sun_tint`=0). A sphere → compare its **top** vertex (N≈+Y, zenith) vs an **equator** vertex (N≈horizontal). The top's ambient is **brighter AND bluer**: `lum(top) > lum(side)` and `vcol(top).z/lum(top) > vcol(side).z/lum(side)` (more blue-weighted). Confirms the gradient + cool tint.
- **Sun tint (warmth + direction):** one scene, a single light off to **one side** (e.g. +X). Compare two vertices **both on the shadowed hemisphere** (ambient-only, so the direct light doesn't swamp it): the one whose normal points **toward** the light (higher `dot(N,L)`, larger wrap) is **warmer** — `vcol.x/lum` (red fraction) is higher than the away-facing vertex's. Confirms the directional warm fill reaches past the terminator. (With no light, this asymmetry vanishes — a companion no-light check that both are equal warmth.)
- **AO oracle still holds (unchanged):** tessellated floor + sphere, no lights → `lum(near) < lum(far)`. The two floor faces share the **same normal**, so `sky_ambient` is identical between them; only AO differs ⇒ near still darker. This test should pass **with no edit** — verify it does (a regression canary for the AO×ambient composition).
- **Update the existing baselines:** the sphere-lit test and the no-self-black test hardcode `kAmbient=0.45`. Re-express them against the new model: no-self-black → AO=1 so a vertex's `vcol == base_albedo*(sky_ambient(N)+sun_tint(N))*exposure` (compute via the same `detail` helpers, not a magic number); sphere-lit → keep relational (`top_lum > its own ambient seed`). Do NOT leave a stale `0.45` assertion.
- **Determinism:** `same_face`/`vcol` byte-equality across two builds still holds.
- **φ-skip:** the existing φ test (vcol == pure colormap) still passes (ambient bypassed in wave mode).
- **Visual:** before/after (flat-grey ambient vs sky+sun) of headless `step1_add_object` raster → `docs/superpowers/artifacts/2026-06-06-sky-ambient-before-after.png`. Expect cool sky tint on up-faces, a warm fill on the light side, grey flatness gone.

## 6. Cost / when it runs
Two extra Vec3 ops + one `dot`/`normalize`/`light_center` per vertex inside `shade_face` — `O(verts)`, dwarfed by AO's `O(verts × kAoRays × entities)` ray casts right beside it. `build_sw` runs on `apply(Op)`/`enable_sim`; **not** per-frame in non-wave live (cached `sw_` re-rasterized). In `--wave` the φ-override short-circuits before `shade_face`, so the sky ambient costs nothing there. No new allocation.

## 7. Scope boundary (YAGNI)
**In:** hemispheric sky ambient by world-up normal + soft directional sun tint (half-Lambert wrap), fixed `constexpr` sky/sun constants, AO-occluded, calibrated to the prior ambient level. **Out (hooks kept):** per-scene configurable sky in the bake (the preview uses fixed constants like every other shading constant — if the app's `kSky` should drive it later, plumb a `Sky` into `LoweredScene` then; noted, not built); true sky IBL / cosine-weighted hemisphere irradiance (the path tracer is the GI truth; this is a cheap analytic preview); multiple sun/area-light fills (one dominant light is enough for a preview); view-dependent ambient (no camera in the bake, and unnecessary — ambient is view-independent). All four constants (`kSkyZenith`, `kSkyHorizon`, `kSunColor`, `kSunStrength`) are `constexpr` tunables.
