# Design Spec — Per-vertex Ambient Occlusion (visual slice 4)

**Goal:** add **ambient occlusion** to the raster preview — darken the AMBIENT term where a vertex's hemisphere is occluded by nearby geometry (crevices, the floor under/around objects, object-object contact), so the preview reads "rendered" + grounded, closing more of the raster↔path-trace gap. Date 2026-06-05 · Status: DRAFT. Reuses the contact-shadow `seg_hits_*` ray primitives.

Context: visual slice after AA + contact-shadows/tessellation + exposure/diff. Contact shadows darken the DIRECT light (occlusion toward a light); AO darkens the AMBIENT (occlusion over the hemisphere) — complementary. Stays in `build_sw` (the bridge), `render.sw` graph-free.

## 1. Approach

A per-vertex AO factor `ao ∈ [0,1]` baked into `shade_face`, multiplying **only the ambient term** (`base_albedo·kAmbient·ao`); direct light + shadows + self-emit unchanged. For a shaded point with unit normal N, cast `kAoRays` FIXED directions over the hemisphere about N; `ao = 1 − (occluded / kAoRays)` where a direction is occluded if a ray from the (normal-biased) point hits any *other* entity within `kAoDist`. **Deterministic** (fixed directions, fixed order, pure f32) — no random sampling. Reuses `seg_hits_sphere/quad/tri(p, dir, len, geom)` with `len = kAoDist` (a max-distance RAY hit, vs the shadow's segment-to-light).

## 2. Components

### 2.1 Fixed hemisphere directions (`build_sw` detail)
A `constexpr` set of `kAoRays` unit directions in TANGENT space (z = up), biased toward the hemisphere (e.g. 1 straight up + 2 rings of cosine-ish elevations). E.g. `kAoRays = 9`: `(0,0,1)` + 8 at ~45–60° elevation around the azimuth. Stored as a `std::array<Vec3, kAoRays>`. These are rotated to world via a deterministic orthonormal basis built from N.

### 2.2 Tangent basis (deterministic, branchless)
```cpp
// Orthonormal (T,B,N) from a unit normal N — branchless Duff/Frisvad (no T/B
// discontinuity at component ties). Deterministic pure-f32.
inline void onb_from_normal(Vec3 N, Vec3& T, Vec3& B) noexcept;
```

### 2.3 `ambient_occlusion(point, N, occluders, self) -> f32` (`build_sw` detail)
```cpp
[[nodiscard]] f32 ambient_occlusion(Vec3 point, Vec3 N,
                                    const std::vector<LoweredEntity>& occluders,
                                    NodeId self) noexcept;
```
**REQUIRED — AO-normal reorientation (the floor normal points DOWN).** `emit_quad`'s
`n = cross(u,v)` for the floor `u=(8,0,0), v=(0,0,8)` is `(0,−64,0)` — straight −Y;
the existing Lambert survives this only because `two_sided` uses `|N·L|` (sign-blind),
but AO is sign-SENSITIVE: a downward N opens the hemisphere below the floor → the
sphere above is never sampled → the floor would NOT darken (the §5 oracle would fail).
So `ambient_occlusion` reorients internally to the world-up side **for the AO sample
only**: `const Vec3 N_ao = (dot(N, Vec3{0,1,0}) < 0) ? N*-1 : N;` (the y=0 ground
convention; the more general eye-facing flip would need an eye param `shade_face`
lacks today — noted follow-up). Then `p0 = point + N_ao·kAoBias`; `onb_from_normal(N_ao, T, B)`.
For each of the `kAoRays` tangent dirs `d_t`: world dir `d = T·d_t.x + B·d_t.y + N_ao·d_t.z`;
a ray `seg_hits_*(p0, d, kAoDist, occ.geom)` for each occluder with `source != self`
(std::visit, mirroring the shadow path) → occluded if any hit. `ao = max(kAoFloor,
1 − occluded/kAoRays)` (never fully black). Skips `self` so a convex sphere isn't
self-darkened; the floor's AO comes from OTHER entities (the sphere) — the point.
(The caller passes the RAW signed N; the up-flip is internal so the Lambert path keeps
the raw N and `vcol` byte-determinism is preserved. `emit_tri`'s `cross(b−a,c−a)` has
the same arbitrary sign → same internal flip applies, automatically.)

### 2.4 `shade_face` multiplies ambient by AO
`shade_face(point, normal, albedo, emit, lights, two_sided, occluders, self)` already exists (contact shadows) and computes the unit normal `N`. Change the ambient line from `lit = base_albedo*kAmbient + self_emit` to compute `const f32 ao = ambient_occlusion(point, N, occluders, self); lit = base_albedo*(kAmbient*ao) + self_emit;` (pass the RAW signed `N` — `ambient_occlusion` does its own up-flip). Everything downstream (direct light × shadow-visibility, atten, exposure `kRasterExposure`) unchanged — AO multiplies ONLY the ambient seed, the shadow `vis` multiplies ONLY the disjoint direct addend, so no double-darkening. The φ-override fast path still skips `shade_face` entirely → AO not computed in wave mode.

### 2.5 Constants (near `kShadowEps`/`kShadowBias`)
```cpp
inline constexpr int              kAoRays  = 9;       // 1 up + 8 around (~45–60° elev)
inline constexpr aleph::math::f32 kAoDist  = 2.0f;    // local occlusion radius (world units)
inline constexpr aleph::math::f32 kAoBias  = kShadowBias;  // 2e-3, reuse the shadow bias
inline constexpr aleph::math::f32 kAoFloor = 0.15f;   // AO never blacks the ambient below this
```

## 3. Determinism
Fixed `constexpr` hemisphere dirs; `onb_from_normal` is a deterministic pure-f32 basis; occluders iterated in `entities` order; `std::visit` order-independent per occluder. Same `LoweredScene` ⇒ byte-identical `vcol`. `test_build_sw`'s `same_face`/`vcol` oracle holds. `--wave`/`--headless` byte-identity unaffected (wave skips shade).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions. Degenerate normal (`|N|≈0`) → `onb` falls back to a fixed basis; `ao=1` (unoccluded) on any degeneracy so AO never wrongly blacks a surface.

## 5. Testing (`tests/edit/test_build_sw.cpp`)
- **AO oracle:** a tessellated floor + a sphere resting on it (no lights needed for AO — it's about the ambient). A floor face *directly under/beside the sphere* (its hemisphere partly blocked by the sphere) has **lower `vcol` luminance** than a floor face far away (open hemisphere). `lum(near) < lum(far)`. (Distinct from the shadow oracle, which needs a light; AO darkens even an unlit-direction ambient.)
- **No self-black:** a lone sphere with NO other geometry → its AO == 1 everywhere (a convex sphere doesn't self-occlude its outward hemisphere, and `self` is skipped) → its `vcol` is NOT darkened vs the no-AO baseline. **Tests to UPDATE:** the existing contact-shadow test's sphere-lit assertion (`test_build_sw.cpp:333-336`, hardcoded `ambient_lum = (0.8+0.2+0.2)·0.45`, `top_lum > ambient_lum`) now seeds ambient as `base_albedo·(kAmbient·ao)`; it passes IFF the sphere's own AO == 1 — make that explicit (assert sphere AO == 1, or recompute the baseline with `·ao`).
- **Determinism:** `same_face`/`vcol` byte-equality across two `build_sw_scene` calls still holds.
- **φ-skip:** with a per-entity `phi`, `vcol` == the pure colormap (AO/shade bypassed) — the existing φ test covers it.
- **Visual:** before/after (shadows-only vs +AO) of headless `step1_add_object` raster via `visual_review.sh`; expect soft darkening pooling under/around objects + at the floor-sphere contact, beyond the cast shadow.

## 6. Cost / when it runs
`build_sw` runs on `apply(Op)`/`enable_sim` (the live non-wave raster re-rasterizes the cached `sw_`, so AO is NOT per-frame there). In WAVE mode `controller.step()` calls `rebuild_backends_from_prev()` → `build_sw_scene` **every frame**, but AO costs nothing there because the φ-override short-circuits before `shade_face`. Cost `O(verts × kAoRays × entities)` per edit; the 16×16-cell floor shades **~1024 corner points (4/cell, undeduped)** + spheres (~360 verts each) × kAoRays=9 × entities — sub-ms for editor/lattice scenes. **Do NOT unify AO with the φ colormap path** or it becomes O(verts × kAoRays × lattice-N) per frame in `--wave`. Broad-phase (AABB reject before `seg_hits_*`) is the noted hook if a large scene bites.

## 7. Scope boundary (YAGNI)
**In:** per-vertex hemisphere-ray AO on the ambient term, fixed deterministic directions, `kAoFloor` so it stays dim-not-black, normal bias. **Out (hooks kept):** SSAO/screen-space AO (the path tracer's GI is the truth; this is a cheap analytic preview); cosine-importance or many-ray AO (a fixed small set is enough for a preview); AO on the path-trace (already has GI); a broad-phase accelerator (linear scan fine at these sizes). `kAoRays`/`kAoDist`/`kAoBias`/`kAoFloor` are `constexpr`.
