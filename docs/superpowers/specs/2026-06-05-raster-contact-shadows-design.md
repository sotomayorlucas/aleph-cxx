# Design Spec — Raster contact shadows + quad tessellation (visual-alignment slice 2 of 3)

**Goal:** ground the raster preview's objects with **cast/contact shadows** (the floor darkens under a sphere; objects shadow each other) AND fix the flat/faceted floor with a lighting **gradient** — both unlocked by tessellating quads. Date 2026-06-05 · Status: DRAFT (revised after adversarial review).

Context: slice 2 of "align raster with path-trace" (after SSAA; before exposure+diff). The path tracer already has real soft shadows + a lit floor; this gives the cheap preview an analytic approximation. Stays in `build_sw` (the bridge — `render.sw` stays graph-free).

## 0. Why tessellation is REQUIRED (adversarial-review finding)

Per-vertex (Gouraud) shadows are **invisible on the current floor**: it is `QuadLocal{(-4,0,-4),(8,0,0),(0,0,8)}` emitted as 2 triangles shaded only at its 4 corners (±4,0,±4) (`build_sw.cppm` `emit_quad`). The sphere's shadow falls near the origin where **no floor vertex exists**, so interpolation from the far corners darkens nothing. Therefore this slice **tessellates `QuadLocal` into an N×N grid of faces** so interior vertices exist under the shadow. Bonus: per-vertex shading of the grid also gives the floor a real distance-falloff **gradient** (fixes the long-standing flat-floor TODO) and smooths its facets.

**SPEC invariant change:** `QuadLocal` no longer lowers to 2 faces but to `2·Nu·Nv` faces. `tests/edit/test_build_sw.cpp`'s quad face-count oracle is updated accordingly (sphere/tri counts unchanged). `face_source` stays 1:1 (every quad sub-face is sourced from the quad's NodeId → picking unaffected). This is a deliberate, justified change of the earlier "QuadLocal→2 faces" choice.

## 1. Approach

**(A) Tessellate quads** in `emit_quad`: subdivide `QuadLocal{q,u,v}` into `Nu×Nv` cells, each cell a sub-quad (its 4 corners are points on the parallelogram), emitted as the existing 2 triangles per cell with per-vertex shade. `Nu = clamp(ceil(|u|/kCell), 1, kMaxCells)`, `Nv = clamp(ceil(|v|/kCell), 1, kMaxCells)`, `kCell = 0.5` (world units), `kMaxCells = 24`. (Floor 8×8 → 16×16 cells → 512 faces.) Deterministic (pure f32 of u,v).

**(B) Per-vertex light-occlusion shadows** baked into `Face::vcol` via `shade_face`. Today it sums each light's diffuse `albedo⊙emit·max(0,N·L)·atten·kLightScale`. We multiply each light's term by a **visibility `V ∈ [0,1]`**: fraction of light samples whose segment from the shaded point is unoccluded by the *other* scene entities. **Ambient is NOT shadowed** (shadowed regions keep `kAmbient` → dim-not-black, matching the tracer's GI-filled shadows). A small **normal offset** at the start point avoids contact acne/peter-panning.

## 2. Components

### 2.1 Quad tessellation — `emit_quad`
Replace the single-cell `p0..p3` emit with a double loop over `Nu×Nv`: for cell `(i,j)`, corners `c00 = q + u·(i/Nu) + v·(j/Nv)`, `c10 = …(i+1)…`, etc.; shade the 4 corners (`shade_face`, two_sided=true) and `push_tri` the two triangles `{c00,c10,c11}` and `{c00,c11,c01}` (matching the existing winding). The flat normal `n = cross(u,v)` is shared. `source` is the quad's NodeId for every sub-face.

### 2.2 Occlusion primitives (`build_sw` detail)
```cpp
// Hit if the open segment from p along unit `dir` for length `seg_len` strikes the
// primitive at t ∈ (kShadowEps, seg_len − kShadowEps).
bool seg_hits_sphere(Vec3 p, Vec3 dir, f32 seg_len, const SphereLocal&) noexcept; // analytic
bool seg_hits_quad  (Vec3 p, Vec3 dir, f32 seg_len, const QuadLocal&)   noexcept; // ray-plane(n=u×v); express hit−q in (u,v) ⇒ s,t∈[0,1]
bool seg_hits_tri   (Vec3 p, Vec3 dir, f32 seg_len, const TriLocal&)    noexcept; // Möller–Trumbore
```
Guards: `|dot(dir,n)| < 1e-8` (quad/tri) → no hit; zero-area primitive → no hit; degenerate (`seg_len < 2·kShadowEps`) → no hit. `kShadowEps = 1e-3`.

### 2.3 Visibility — `light_visibility(point, normal, light, occluders, self) -> f32`
```cpp
f32 light_visibility(Vec3 point, Vec3 N_unit, const LoweredEntity& light,
                     const std::vector<LoweredEntity>& occluders, NodeId self) noexcept;
```
- **Start-point bias:** `p0 = point + N_unit · kShadowBias` (`kShadowBias = 2e-3`) so the surface doesn't self-hit at the contact.
- **Light samples:** for an area (`QuadLocal`) light, a `kShadowSamples×kShadowSamples` grid across `(q,u,v)` cell-centres (`kShadowSamples = 2` → 4 samples → soft penumbra); for sphere/other geometry, the centre (1 sample). `light_center`/sampling reuse the variant.
- For each sample `s`: `dir = normalize(s − p0)`, `seg_len = |s − p0|`. Occluded if ANY `occluders[k]` with `source != self` AND `source != light.source` has `seg_hits_*` (dispatched via `std::visit` on `occluders[k].world_geometry`, mirroring `emit_entity`).
- `V = (# unoccluded samples) / (total samples)`.

### 2.4 `shade_face` gains shadows
Signature `shade_face(point, normal, albedo, emit, lights, two_sided)` → add `(occluders, self)`. Inside the light loop, after the diffuse term `td` for light `L`, multiply: `td *= light_visibility(point, N_unit, L, occluders, self)` where `N_unit` is the already-normalized shading normal. Ambient/self-emit unchanged.

### 2.5 Threading (mechanical; all in `detail`, no external callers)
`emit_quad/tri/sphere(..., lights, source, phi)` → add `const std::vector<LoweredEntity>& occluders`; `emit_entity(out, e, lights, phi)` → `(out, e, lights, occluders, phi)`; `build_sw_scene` passes `ls.entities` as `occluders`. **The `phi`-override fast path keeps short-circuiting `shade_face`** (`phi ? fc : shade_face(...)`), so shadows are NOT computed in wave mode.

## 3. Determinism
Pure `f32`; occluders iterated in `entities` order; fixed tessellation + sample grids; `std::visit` dispatch is order-independent per occluder. Same `LoweredScene` ⇒ byte-identical `vcol` and face set. `test_build_sw`'s `same_face`/`vcol` byte-equality oracle still holds. `--wave`/`--headless` byte-identical contracts unaffected (wave skips shade; headless is pure).

## 4. Error handling (`aleph_flags_isa`)
No allocation, no exceptions. All degeneracies (zero-length segment, zero-area primitive, grazing `dot≈0`) → treat as **not occluded** (return no-hit / V toward lit) so a light at the surface never self-shadows to black. `Nu,Nv ≥ 1` always (clamp).

## 5. Testing (`tests/edit/test_build_sw.cpp`)
- **Quad face count (updated oracle):** a `QuadLocal` of size `|u|,|v|` lowers to `2·Nu·Nv` faces (with the spec's `kCell`/`kMaxCells`); update the existing 2-face assertion to the formula. Sphere/tri counts unchanged. `face_source` for every quad sub-face == the quad's NodeId.
- **Shadow oracle (now visible):** overhead area light at `y=3`, sphere at origin r=0.5, **tessellated** floor at `y=0`. A floor face whose centroid is **directly under the sphere** (segment to the light passes through the sphere) has **lower `vcol` luminance** than a floor face far away. `lum(under) < lum(far)`.
- **Self/light skip:** a sphere front face (facing the light) keeps `visibility > 0` (not wrongly self-shadowed); assert it stays lit.
- **φ skips shadows (invariant guard):** with a per-entity `phi` passed, the emitted `vcol` equals the pure-colormap colour (no shadow/Lambert mixed in) — i.e. the φ path provably bypasses `shade_face`/`light_visibility`. (Reuse the existing `build_sw phi` test's structure.)
- **Determinism:** existing `same_face`/`vcol` byte-equality across two `build_sw_scene` calls still passes.
- **Visual:** before/after (no-shadow flat floor vs contact-shadow + graded floor) of headless `step1_add_object` raster via `visual_review.sh`.

## 6. Cost / when it runs
`build_sw_scene` runs inside `rebuild_backends_from_prev`, which IS called per frame in the live **wave** loop (`controller.step()` → `aleph.edit-controller.cppm`), but the wave path **skips `shade_face` via the `phi ?` short-circuit**, so shadows/tessellation-shading cost nothing there. In the **normal editor** it runs on `apply(Op)`/`enable_sim` (edit-time), and the per-frame raster re-rasterizes the cached `sw_`. So shadow cost is paid once per edit: `O(verts × lights × kShadowSamples² × entities)`. Honest budget: a sphere shades `kSphereRings·kSphereSectors` ring-vertices (~12·16 = 192 cells, ~360 distinct verts) and a tessellated 8×8 floor adds `(Nu+1)(Nv+1) ≈ 17² ≈ 289` verts; with a few entities + 1–2 lights × 4 samples this is ~10⁵–10⁶ seg-tests per edit — fine synchronously. **Broad-phase hook:** if a scene exceeds ~50 entities or an edit-time stall is observed, add a per-occluder world AABB / bounding-sphere reject before the exact `seg_hits_*` (linear pre-test). Not built now.

## 7. Scope boundary (YAGNI)
**In:** quad tessellation (grid), analytic per-vertex contact/cast shadows (soft via 2×2 area sampling), ambient unshadowed, normal-offset bias, the bonus floor gradient. **Out (hooks kept):** AABB broad-phase (linear scan fine at these sizes); screen-space/ray-traced shadows (the tracer is the truth); **ambient occlusion** proper (crevice darkening independent of lights) — a noted follow-up; adaptive/curvature-aware tessellation (fixed `kCell` grid is enough); shadowing the path tracer (already correct). `kCell`/`kMaxCells`/`kShadowSamples`/`kShadowEps`/`kShadowBias` are `constexpr`.
