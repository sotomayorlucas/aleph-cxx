# Design Spec — Polish: Finer Spheres + Softer Shadows (visual slice 4d)

**Goal:** two low-risk preview-quality bumps in `build_sw`: (1) **finer sphere tessellation** so silhouettes read round instead of faceted (and Gouraud shading — including the later material specular — interpolates smoothly), and (2) **softer contact shadows** so the penumbra is a smooth gradient instead of the current 5-level banding. Date 2026-06-06 · Status: DRAFT. Visual slice 4d (reordered ahead of materials 4c — finer spheres are a prerequisite for the material-kind specular to land on a vertex; see [[materials-slice-parked]]). Builds on 4a (AO) + 4b (sky ambient).

Context (verified): spheres tessellate to `kSphereRings=12 × kSphereSectors=16` (= 384 faces; the exported `kSphereRings/kSphereSectors` drive both `emit_sphere` and the face-count test oracle). Contact shadows sample area lights on a `kShadowSamples=2` (2×2 = 4-sample) grid → visibility ∈ {0, .25, .5, .75, 1}, a visibly banded penumbra. **Both are governed by `constexpr` constants over already-general loops** (`light_visibility`'s `std::array<…, kShadowSamples*kShadowSamples>` + `for i<kShadowSamples`; `emit_sphere`'s ring/sector loops). Shade is baked **per-edit** (the live non-wave raster re-rasterizes the cached `sw_`; `--wave` φ-skips `shade_face`) — so the extra shade cost (more shadow/AO casts) is per-`apply(Op)`, NOT per-frame. Stays entirely in `build_sw`; `render.sw` untouched.

## 1. Approach

- **Finer spheres:** raise `kSphereRings 12→20`, `kSphereSectors 16→28` (384 → 1120 faces/sphere, 2.9×). Vertex angular spacing drops from 15°×22.5° to 9°×12.9° — a markedly rounder silhouette and smoother Gouraud terminator/highlight. `emit_sphere`/`on_sphere` already loop over the constants, so this is a constant change; the face-count test keys off the constants (not literals) and stays green by construction.
- **Softer shadows:** raise `kShadowSamples 2→4` (4×4 = 16 samples). The 2×2 grid can only land visibility on quarter-steps `{0,.25,.5,.75,1}`; 4×4 resolves 1/16ths, so wherever the penumbra spans multiple floor cells the gradient is visibly less banded. (On `make_shadow_scene`'s small-light geometry the penumbra is a single thin ring — the granularity gain is the per-corner *value* moving from a quarter-step to a 16th, e.g. 0.75→0.625 at the same corner; on a wider penumbra it reads as a smoother ramp.) The `std::array` extent is `kShadowSamples*kShadowSamples` (auto-resizes) and the sampling loops are already general, so this is a constant change. It **smooths** the penumbra gradient; it does not **widen** it (penumbra width is the light's solid angle — a scene/light-size change, out of scope).

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
- **Face-count oracle stays green (regression):** the existing `1 sphere + 1 quad → SPEC face counts` test asserts `sphere_faces == kSphereRings*kSphereSectors*2` using the **exported constants** — it auto-tracks the new resolution (no literal `384`). Verify it still passes (i.e. no hidden literal). Add an explicit pin that the polish target was met: `CHECK(kSphereRings >= 20); CHECK(kSphereSectors >= 28);` (so a later accidental down-tune is caught) AND a smoothness metric: the polar step `π/kSphereRings <= 0.18 rad` (clearly passed by 20 rings ≈0.157, clearly failed by the old 12 rings ≈0.262 — a meaningful "rounder than before" floor, not a knife-edge against exactly 20).
- **Softer-shadow granularity oracle (direct, geometry-robust):** assert the area-light sampling now resolves visibility in **1/16ths** — a value the 2×2 (quarter-step) grid cannot produce. (Do NOT use a luminance "band" or a "≥N distinct levels" count: on `make_shadow_scene`'s specific geometry the penumbra is a single thin ring of corner faces at ONE intermediate level, and those corners sit *closer* to the light so atten makes them brighter than `lum_far` — i.e. outside any [under,far] band and identical-looking at 2×2 vs 4×4. Verified by the review.) Instead, in `make_shadow_scene`, iterate the floor faces (`face_source==NodeId{2}`), and for each take its `verts[0]` as a world-space probe `p`; call `detail::light_visibility(p, Vec3{0,1,0}, ls.lights[0], ls.entities, NodeId{2})`. Assert **there EXISTS a probe with a genuine 16th-but-not-quarter-step visibility**: `v ∈ (0,1)` AND `|16v − round(16v)| < 1e-3` AND `|4v − round(4v)| > 1e-3` (e.g. the penumbra corner's `v = 10/16 = 0.625`, unrepresentable on a 2×2 grid). This directly proves the finer sampling granularity without depending on luminance, atten, or how many cells fall in the penumbra. Pin `kShadowSamples >= 4`.
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
