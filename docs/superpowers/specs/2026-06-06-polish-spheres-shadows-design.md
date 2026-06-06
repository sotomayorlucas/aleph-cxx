# Design Spec — Polish: Finer Spheres + Softer Shadows (visual slice 4d)

**Goal:** two low-risk preview-quality bumps in `build_sw`: (1) **finer sphere tessellation** so silhouettes read round instead of faceted (and Gouraud shading — including the later material specular — interpolates smoothly), and (2) **softer contact shadows** so the penumbra is a smooth gradient instead of the current 5-level banding. Date 2026-06-06 · Status: DRAFT. Visual slice 4d (reordered ahead of materials 4c — finer spheres are a prerequisite for the material-kind specular to land on a vertex; see [[materials-slice-parked]]). Builds on 4a (AO) + 4b (sky ambient).

Context (verified): spheres tessellate to `kSphereRings=12 × kSphereSectors=16` (= 384 faces; the exported `kSphereRings/kSphereSectors` drive both `emit_sphere` and the face-count test oracle). Contact shadows sample area lights on a `kShadowSamples=2` (2×2 = 4-sample) grid → visibility ∈ {0, .25, .5, .75, 1}, a visibly banded penumbra. **Both are governed by `constexpr` constants over already-general loops** (`light_visibility`'s `std::array<…, kShadowSamples*kShadowSamples>` + `for i<kShadowSamples`; `emit_sphere`'s ring/sector loops). Shade is baked **per-edit** (the live non-wave raster re-rasterizes the cached `sw_`; `--wave` φ-skips `shade_face`) — so the extra shade cost (more shadow/AO casts) is per-`apply(Op)`, NOT per-frame. Stays entirely in `build_sw`; `render.sw` untouched.

## 1. Approach

- **Finer spheres:** raise `kSphereRings 12→20`, `kSphereSectors 16→28` (384 → 1120 faces/sphere, 2.9×). Vertex angular spacing drops from 15°×22.5° to 9°×12.9° — a markedly rounder silhouette and smoother Gouraud terminator/highlight. `emit_sphere`/`on_sphere` already loop over the constants, so this is a constant change; the face-count test keys off the constants (not literals) and stays green by construction.
- **Softer shadows:** raise `kShadowSamples 2→4` (4×4 = 16 samples → visibility in 1/16ths, 17 levels). The `std::array` extent is `kShadowSamples*kShadowSamples` (auto-resizes) and the sampling loops are already general, so this is a constant change. This **smooths the penumbra gradient** (kills the 5-level banding); it does not widen the penumbra (that is set by the light's solid angle — a scene/light-size change, out of scope).

**Determinism preserved:** still fixed-grid sampling + fixed tessellation, pure-f32 — same `LoweredScene` ⇒ byte-identical `vcol`; `same_face` holds (the bake is *more* sampled, not random). **φ-skip preserved** (shadows/AO live in `shade_face`, bypassed in wave mode). **No new math, no new params, no signature changes.**

## 2. Components (`build_sw` detail constants)
```cpp
inline constexpr int kSphereRings   = 20;  // was 12 (latitude bands)
inline constexpr int kSphereSectors = 28;  // was 16 (longitude slices)
inline constexpr int kShadowSamples = 4;   // was 2 (4x4 area-light grid → smooth penumbra)
```
Nothing else changes: `emit_sphere`, `on_sphere`, `light_visibility`, the `std::array` size expression, and the exported `SPHERE_RINGS/SPHERE_SECTORS` aliases all already reference the constants. Confirm `emit_sphere`/`on_sphere` use `kSphereRings/kSphereSectors` (not literals) before changing — if any literal `12`/`16` is hard-coded there, replace it with the constant.

## 3. Cost
Per-edit bake (the only place shade runs with shadows/AO):
- Finer spheres: 2.9× the sphere vertices → 2.9× the per-vertex AO (`×kAoRays×entities`) + shadow casts on spheres. Editor scene (1–2 spheres) → still sub-ms to a few ms per edit. Acceptable.
- Softer shadows: 4× the per-vertex-per-light shadow casts (4→16 samples). Editor (1 light) → modest.
- **Wave lattice caveat (the one real cost):** the `--wave` lattice has many sphere entities; finer spheres multiply the **faces rasterized per frame** by 2.9× (the per-frame cost there is rasterization, NOT shade — `--wave` φ-skips `shade_face`, so no extra AO/shadow cost). If the software rasterizer's per-frame face throughput becomes the bottleneck on the lattice, the §7 hook (per-scene sphere resolution) applies. The bake-time shade cost does NOT hit the lattice (wave φ-skips it).

## 4. Error handling (`aleph_flags_isa`)
No allocation beyond the (now larger, still fixed-size `constexpr`) `std::array<…, 16>` shadow-sample buffer (stack, `kShadowSamples*kShadowSamples`=16). No exceptions. The face-count formula is exact for any rings/sectors ≥ 1.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
- **Face-count oracle stays green (regression):** the existing `1 sphere + 1 quad → SPEC face counts` test asserts `sphere_faces == kSphereRings*kSphereSectors*2` using the **exported constants** — it auto-tracks the new resolution (no literal `384`). Verify it still passes (i.e. no hidden literal). Add an explicit pin that the polish target was met: `CHECK(kSphereRings >= 20); CHECK(kSphereSectors >= 28);` (so a later accidental down-tune is caught) AND a smoothness metric: the polar step `π/kSphereRings < 0.16 rad` (≈9°) — pins "rounder" geometrically, not just face-count.
- **Softer-shadow gradient oracle:** in `make_shadow_scene` (sphere over a tessellated floor + overhead area light), the penumbra band (floor faces between fully-lit `r>4` and fully-shadowed `r≈0`) now exhibits a **smooth multi-level gradient**. Collect the distinct floor-face luminances strictly between the fully-shadowed-under value and the fully-lit-far value; assert there are **≥3 distinct intermediate penumbra levels** (a 2×2 grid yields at most the coarse {.25,.5,.75} set and, on this geometry, fewer actually-occupied levels — 4×4 yields a finer gradient). Pin `kShadowSamples >= 4`. (Robust: asserts the gradient is finer, not an exact count.)
- **Contact-shadow + AO oracles still hold (regression):** `lum_under < lum_far` (shadow) and `lum_near < lum_far` (AO) unchanged — finer spheres + more shadow samples don't invert them. Run them.
- **Determinism:** `same_face` byte-equality across two builds holds (more samples, still deterministic).
- **φ-skip:** the existing φ test still passes (shadows/AO bypassed in wave mode).
- **Visual:** before/after (12×16 + 2×2 vs 20×28 + 4×4) of the headless `step1_add_object` raster → `docs/superpowers/artifacts/2026-06-06-polish-spheres-shadows-before-after.png`. Expect a visibly rounder sphere silhouette + a smooth (un-banded) contact-shadow penumbra.

## 6. Determinism / when it runs
`build_sw` runs on `apply(Op)`/`enable_sim`; the extra cost is per-edit, not per-frame (non-wave live re-rasterizes the cached `sw_`; `--wave` φ-skips shade). Byte-identical run-to-run. No committed raster PPM gates today (only before/after PNG artifacts; the AO/sky artifacts were rendered at 12×16 and need no re-bless — they are not tests).

## 7. Scope boundary (YAGNI)
**In:** raise the three resolution constants (sphere rings/sectors, shadow samples), keeping every loop/array/test general over them. **Out (hooks kept):**
- *Per-scene / adaptive sphere resolution* — a single global `constexpr` keeps determinism + the face-count oracle simple; if the `--wave` lattice's per-frame rasterization of 2.9× faces bites, split into an editor-res vs lattice-res parameter then (noted, not built).
- *Wider (not just smoother) penumbra* — penumbra width is the light's solid angle; widening it is a scene/light-size change, not a `build_sw` sampling change.
- *Spherical-cap / geodesic sphere* — the UV sphere is fine at this resolution; a geodesic tessellation (uniform triangles, no pole pinching) is a larger change for marginal preview gain.
- *Jittered / stratified shadow samples* — a fixed grid stays deterministic and is smooth enough at 4×4; jitter would need a deterministic per-point seed for no real preview gain.
The three constants remain `constexpr` tunables.
