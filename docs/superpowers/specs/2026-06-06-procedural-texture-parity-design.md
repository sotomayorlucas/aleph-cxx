# Design Spec — Procedural Checker Texture, Raster↔Path-Trace Parity (visual slice 4c-ii)

**Goal:** add a deterministic **procedural checker** texture that renders **identically aligned** in the software rasterizer AND the path tracer — closing the last materials gap (the user chose "ambos": materials 4c-i + texture 4c-ii). Today: the graph has no textured material, the raster bakes ZEROED UVs, and the PT's `sample_textured_albedo` is a **grey stub**. This slice wires `TexturedLambertian` through the whole stack so a **textured floor** shows the same checker squares in both backends. Date 2026-06-06 · Status: DRAFT. Second of the materials+texture pair (after 4c-i). The path tracer is the truth; this makes the raster preview agree on the texture *pattern*.

Context (verified — explorer `ad3a998f`): graph `aleph::types::MaterialKind` = {Lambertian, Metal, Dielectric, Emissive} (no Textured); `aleph::types::Material` has no `uv_scale`. Scene `aleph::scene::MaterialKind` HAS `TexturedLambertian` + `TexturedLambertianSoA{tex_id, uv_scale}` (no albedo; image-backed) + `scene_add_textured_lambertian`. PT `sample_textured_albedo` (rt-material.cppm) is a grey stub; it's the single albedo source for the TexturedLambertian scatter+NEE. Raster `Face::tex` is a `TexSampleFn`(`u32(*)(f32,f32)`); `rast_scan` does `argb_to_linear(tex(u,v)) × vcol` per pixel; `tex_checker(u,v)=((⌊u⌋^⌊v⌋)&1)?light:dark` exists; `push_tri` hardcodes zeroed UVs + `&tex_white`. The 4c-i materials slice already threads `const MaterialParams& mat` into `emit_quad/tri/sphere`.

## 1. The UV-parity contract (the crux)
For the checker to ALIGN between backends, both must feed `checker()` the SAME (u,v) for a surface point:
- **Quad (the floor — the deliverable):** PT `hit_quad` (scene-hit.cppm) sets `rec.u=α, rec.v=β` = fractional position along `(u_edge, v_edge)` ∈ [0,1]². Raster `emit_quad` corner `P(i,j)` IS at `(s,t)=(i/Nu, j/Nv)` of `(q,u,v)` — so baking vertex UV `(i/Nu, j/Nv)` matches the PT's α,β **exactly** after perspective-correct interpolation. **Quad parity is exact and trivial.**
- **Sphere:** PT uses `u=(atan2(−z',x')+π)/2π`, `v=acos(−y')/π` (y' up). Raster `on_sphere(ring,sector)` has `y'=cos(πring/RINGS)` so the PT's `v=1−ring/RINGS` and `u=(π−2π·sector/SECTORS)/2π` (mirrored). To match, the raster bakes the **PT formula** per vertex: `v = acos(−ŷ)/π`, `u = (atan2(−ẑ,x̂)+π)/2π` where `(x̂,ŷ,ẑ)=normalize(vertex−center)` — NOT the naive `sector/SECTORS`. (A textured sphere is supported + consistent, but the TESTED/artifact subject is the floor.)
- **Tri:** PT leaves `rec.u=rec.v=0` (no UV) — a textured tri cannot have parity; **out of scope** (documented; the floor is a quad).
- **uv_scale** (tiling frequency): the raster bakes `uv·uv_scale` at vertices; the PT multiplies `rec.u/v` by `uv_scale` inside the sampler — both then feed `checker((s,t)·uv_scale)`. Same tiles.

## 2. The shared checker (alignment is parity; exact brightness need not bit-match)
Both renderers already differ in shading (Gouraud+no-GI vs path-traced), so PARITY here means **the tile boundaries + light/dark sense align**, not bit-identical brightness. Shared definition (both backends):
```
cell(u,v) = ((⌊u⌋ ^ ⌊v⌋) & 1)        // u,v already × uv_scale
```
- **Raster:** a new `TexSampleFn tex_checker_uv` returning `cell ? 0xFFFFFFFF : kCheckerLoArgb` (`0xFFB0B0B0`). `rast_scan` does `argb_to_linear(·) × vcol` → the HI cell = `1.0×vcol`, LO = `argb_to_linear(0xB0)≈0.434×vcol`. (Reuse the existing `tex_checker` only if its grey levels are acceptable; a dedicated fn pins the parity levels.)
- **PT:** `sample_textured_albedo` returns `albedo × (cell ? kCheckerHi : kCheckerLo)` with `kCheckerHi=1.0f`, `kCheckerLo=0.434f` (= `argb_to_linear(0xB0)`, pinned so the LO cell matches the raster's after its sRGB decode). The albedo is the material's base albedo (§3.1).
Both → `albedo × {1.0 | 0.434} × lighting`, with aligned tiles. (The raster's `vcol` carries `albedo×shade` since `shade_face` treats TexturedLambertian as Lambertian — §3.4.)

## 3. Components

### 3.1 Graph types (`graph/src/aleph.types`)
- `aleph::types::MaterialKind`: add `TexturedLambertian = 4` (extends the `uint8_t` enum; the existing 4 keep their values).
- `aleph::types::Material`: add `math::f32 uv_scale{4.0f};` (checker tiles across a [0,1] face; 4 = a 4×4 board on a unit-param quad — the 8×8 floor → 4 tiles... pick a sensible default, tune later). Keep `albedo` (the checker's base color).

### 3.2 Lowering (`bridge/src/aleph.lowering`)
- `MaterialParams` (`:lowered`): add `math::f32 uv_scale{4.0f};`. `to_params` (`:lower`) copies `m.uv_scale`.
- `build.cppm` `add_material`: add `case TexturedLambertian: return scene_add_textured_lambertian(s, m.albedo, {m.uv_scale, m.uv_scale});` (the scene-add gains an albedo param, §3.3). (`MaterialKind` cross-layer: graph `types::MaterialKind::TexturedLambertian` → scene `scene::MaterialKind::TexturedLambertian`.)

### 3.3 Scene + PT (`render/src/aleph.scene`, `render/src/aleph.render.rt`)
- `TexturedLambertianSoA`: add `std::vector<math::Vec3> albedo;`. `textured_lambertian_append(s, albedo, tex_id, uv_scale)` pushes albedo too (tex_id kept as a dummy `0` — procedural, no image; nothing else reads it). `scene_add_textured_lambertian(s, albedo, uv_scale)` (drop/default the tex_id param).
- `sample_textured_albedo` (rt-material.cppm): replace the grey stub with:
```cpp
const Vec2  sc = s.tex_lamb.uv_scale[mat_idx];
const Vec3  a  = s.tex_lamb.albedo[mat_idx];
const int   cu = static_cast<int>(std::floor(u * sc.x));
const int   cv = static_cast<int>(std::floor(v * sc.y));
const f32   lvl = ((cu ^ cv) & 1) ? kCheckerHi : kCheckerLo;   // 1.0 / 0.434
return a * lvl;
```
(Pure f32, deterministic. `kCheckerHi/Lo` as `constexpr` in render.rt or a shared render.common header.)

### 3.4 Raster (`bridge/.../aleph.lowering-build_sw.cppm`)
- **`shade_face` kind handling:** `TexturedLambertian` shades exactly like **Lambertian** (it IS lambertian + a texture) — add it to the default branch (or `case TexturedLambertian:` falling through to Lambertian). So `vcol = albedo×shade` (the texture is applied by `f.tex × vcol` at raster time, not in the bake). `self_emit` added (emit=0 default).
- **`push_tri` gains UVs + a tex fn:** `push_tri(out, a,b,c, ca,cb,cc, uva,uvb,uvc, TexSampleFn fn, source)` — set `f.uvs={uva,uvb,uvc,uvc}`, `f.tex=fn`.
- **`emit_quad`:** per corner `P(i,j)` bake `uv=(i/Nu, j/Nv)·uv_scale`; `fn = (mat.kind==TexturedLambertian) ? &tex_checker_uv : &tex_white`. (uv_scale from `mat.uv_scale`.) The two `push_tri` calls pass the 3 corner UVs each.
- **`emit_sphere`:** per vertex bake the **PT-matching** spherical UV `(u,v)·uv_scale` with `u=(atan2(−ẑ,x̂)+π)/2π, v=acos(−ŷ)/π`; same `fn` selection.
- **`emit_tri`:** bake `(0,0)/(1,0)/(1,1)·uv_scale` (no PT parity; `fn=&tex_white` even if textured, since the PT has no tri UV — or checker with a note). Keep `&tex_white` for tri to avoid a mismatched checker.
- `tex_checker_uv` lives in `render.sw-scene_rt.cppm` beside `tex_checker` (a `TexSampleFn`).

### 3.5 Subject
Make the editor floor (`build_initial_graph`) `MaterialKind::TexturedLambertian` (albedo a neutral tan/grey, `uv_scale` tuned to ~4–8 tiles across the 8×8 floor). Both the raster preview and a path-trace of the same scene then show the aligned checker.

## 4. Determinism
The checker is a pure f32 function of (u,v); UVs are fixed per tessellation; no RNG. Raster: same `LoweredScene` ⇒ byte-identical `vcol` AND `f.uvs`/`f.tex` (the `same_face` oracle compares `uvs` + `vcol` — it now also pins the baked UVs). PT: `sample_textured_albedo` is deterministic. `--wave` unaffected (φ-skips shade; the φ path still sets uvs/tex but the colormap overrides vcol — confirm the φ test: vcol == colormap regardless of tex, since tex×vcol happens at RASTER time not bake; the φ test checks `vcol`, so it's unaffected — but a textured φ entity would show checker×colormap at raster time. The wave lattice spheres are NOT textured, so moot).

## 5. Testing
**Raster (`tests/edit/test_build_sw.cpp`):**
- **UVs are baked (not zero):** a TexturedLambertian floor quad → its faces' `f.uvs` are the `(i/Nu,j/Nv)·uv_scale` grid (NOT all-zero), and `f.tex == &tex_checker_uv`; a Lambertian quad keeps `f.tex==&tex_white` + zeroed-ish UVs. Assert a textured face has a non-zero UV and the checker fn.
- **Checker alternates:** `tex_checker_uv(0.5,0.5)` vs `tex_checker_uv(1.5,0.5)` differ (adjacent tiles) and `tex_checker_uv(0.5,0.5)==tex_checker_uv(1.5,1.5)` (diagonal same). Pin the two ARGB levels.
- **shade_face unchanged for Lambertian:** the 4c-i Lambert golden still holds (TexturedLambertian is a NEW kind; existing Lambertian faces are byte-identical). `same_face` determinism (now incl. uvs) holds.
**Scene/PT (`tests/` scene or rt test):**
- **`sample_textured_albedo` checker:** for a `scene_add_textured_lambertian(albedo, {sx,sy})` material, `sample_textured_albedo(s, idx, u, v)` returns `albedo×kCheckerHi` on a HI cell and `albedo×kCheckerLo` on a LO cell; adjacent (u,v) tiles differ; deterministic.
- **Parity (the key oracle):** for a unit floor quad at a chosen world point, the PT's `(α,β)` and the raster's baked `(i/Nu,j/Nv)` at the nearest vertex agree (≤ a cell) so `cell(α·scale,β·scale)` == the raster tile — assert the checker *boolean* matches at a few sample points (a numeric parity check, not a pixel diff).
**Visual:** before/after — the editor floor flat vs checkered (raster) → `docs/superpowers/artifacts/2026-06-06-texture-checker-before-after.png`; AND a raster-vs-PT contact sheet of the textured floor (via `visual_review.sh`) showing the checker tiles ALIGN between backends.

## 6. Cost
Raster: the checker is sampled per-pixel in `rast_scan` (already the live path; `tex_white` was a constant, the checker is a few int ops) — negligible. PT: `sample_textured_albedo` is two `floor`s — negligible. Bake: UV generation is arithmetic in the existing emit loops. No new allocation (the SoA `albedo` vector grows with materials).

## 7. Scope boundary (YAGNI)
**In:** a procedural checker `TexturedLambertian` through graph→lowering→both renderers; real UVs on the bake path (quad exact-parity, sphere PT-matching); a textured floor subject; raster+PT+parity tests. **Out (hooks kept):**
- *Image textures* — the `tex_id`→`Scene::textures` image path stays vestigial; this is procedural-only (deterministic, no asset loading). The image hook remains for a future `aleph_rt` 'earth' scene.
- *Textured tris* — the PT assigns no tri UV (`rec.u=v=0`); a tri checker can't have parity. Tri stays `tex_white`.
- *Other procedural patterns* (brick/wood) — `tex_brick`/`tex_floor` exist but one checker closes the gap; more patterns are a tuning add.
- *Per-material multiple textures / normal maps / mipmapping* — single albedo checker only.
`kCheckerHi/Lo`, the default `uv_scale`, and the checker levels are `constexpr`/material tunables.
