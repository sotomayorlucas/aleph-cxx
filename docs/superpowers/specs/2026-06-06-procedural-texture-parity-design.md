# Design Spec — Procedural Checker Texture, Raster↔Path-Trace Parity (visual slice 4c-ii)

**Goal:** add a deterministic **procedural checker** that renders **identically aligned** on the **floor quad** in the software rasterizer AND the path tracer — closing the last materials gap (the user chose "ambos": materials 4c-i + texture 4c-ii). Today the graph has no textured material, the raster bakes ZEROED UVs, and the PT's `sample_textured_albedo` is a **grey stub**. This slice wires `TexturedLambertian` through the whole stack so a **textured floor** shows the same checker squares in both backends. Date 2026-06-06 · Status: REVISED (adversarial review applied; scoped to the floor QUAD). Second of the materials+texture pair.

Context (verified, explorer `ad3a998f` + review): graph `aleph::types::MaterialKind` = {Lambertian,Metal,Dielectric,Emissive} (no Textured); `aleph::types::Material` has no `uv_scale`. Scene `aleph::scene::MaterialKind` HAS `TexturedLambertian=4` + `TexturedLambertianSoA{tex_id, uv_scale}` (no albedo) + `scene_add_textured_lambertian`. PT `sample_textured_albedo` (rt-material.cppm) is a grey stub — the single albedo source for the TexturedLambertian scatter+NEE. Raster `Face::tex` is a `TexSampleFn`(`u32(*)(f32,f32)`); `rast_scan` does `argb_to_linear(tex(u,v)) × vcol` per pixel where **`argb_to_linear` is a plain `byte/255` (NO sRGB decode)**; `tex_checker` exists; `push_tri` hardcodes zeroed UVs + `&tex_white`. The 4c-i materials slice threads `const MaterialParams& mat` into `emit_quad/tri/sphere`.

## 1. The UV-parity contract — QUAD only (the deliverable)
The checker aligns between backends iff both feed `checker()` the SAME (u,v) for a surface point:
- **Quad (the floor — IN scope, EXACT parity):** PT `hit_quad` (scene-hit.cppm) sets `rec.u=α, rec.v=β` = fractional position along `(u_edge,v_edge)` ∈ [0,1]² (verified: for `P=q+s·u+t·v`, `α=s, β=t` exactly). Raster `emit_quad` corner `P(i,j)` is at `(s,t)=(i/Nu, j/Nv)` of `(q,u,v)` — so baking vertex UV `(i/Nu, j/Nv)` equals the PT's α,β **bit-for-bit at vertices** (and matches per-pixel after perspective-correct interpolation, since u,v are linear on a planar quad). **Trivial, exact.**
- **Sphere — OUT of scope (longitude u-seam).** The per-vertex spherical bake `u=(atan2(−ẑ,x̂)+π)/2π` matches the PT per-vertex, BUT at the sector-wrap the baked `u` jumps ~0→1 across ONE cell column; the rasterizer linearly interpolates that, cramming `uv_scale` checker columns into one cell = a visible garbage stripe (verified: equator seam cell, baked u 0.036→1.000 = ~3.9 tiles at uv_scale=4). The PT recomputes u per-pixel (no smear), so they DIVERGE at the seam. A real fix needs duplicated wrap-column vertices (baking `u=1.0` not `0.0`) or per-pixel UV — not worth it for the floor-checker deliverable. **Spheres stay `tex_white`** (untextured) this slice; noted hook in §7. (Away from the seam the interp error is ~0 tiles — the seam is the only defect, so a seam-only fix is a clean future add.)
- **Tri — OUT (no PT UV).** PT `hit_tri` leaves `rec.u=rec.v=0`; a textured tri can't have parity. Tris stay `tex_white`.
- **uv_scale** (tiling): the raster bakes `uv·uv_scale` at quad vertices; the PT multiplies `rec.u/v` by `uv_scale` inside the sampler — both feed `checker((s,t)·uv_scale)`. Same tiles.

## 2. The shared checker (alignment = parity; brightness pinned via byte/255)
PARITY here = the tile boundaries + light/dark sense align. Shared cell function (both backends):
```
cell(u,v) = ((⌊u⌋ ^ ⌊v⌋) & 1)        // u,v already × uv_scale
```
**Levels — single source of truth is the LO byte** (`argb_to_linear` is plain `/255`, NOT sRGB):
- `kCheckerLoArgb = 0xFF808080` (LO byte `0x80`); `kCheckerHi = 1.0f`; **`kCheckerLo = 128.0f/255.0f` (≈0.502)** — i.e. `argb_to_linear(0x80)` under the real `/255` decode. (HI byte `0xFF` → `argb_to_linear` → `1.0`.)
- **Raster:** a new `TexSampleFn tex_checker_uv` returning `cell ? 0xFFFFFFFF : kCheckerLoArgb`. `rast_scan` → `argb_to_linear(·)×vcol` = `{1.0 | 0.502}×vcol`.
- **PT:** `sample_textured_albedo` returns `albedo × (cell ? kCheckerHi : kCheckerLo)` = `albedo×{1.0 | 0.502}`.
Both → `albedo × {1.0|0.502} × lighting`, aligned tiles. (Raster `vcol` already carries `albedo×shade` since `shade_face` treats TexturedLambertian as Lambertian — §3.4; albedo appears ONCE in each path, no double-albedo.)

## 3. Components

### 3.1 Graph types (`graph/src/aleph.types`)
- `aleph::types::MaterialKind` (attribute.cppm): add `TexturedLambertian = 4` (extends the `uint8_t` enum; existing values unchanged). **Cross-layer:** scene `MaterialKind::TexturedLambertian` is already `=4` (handle32.cppm:20) — the values align 1:1; §5 pins it with a graph-side test.
- `aleph::types::Material` (node.cppm): add `math::f32 uv_scale{4.0f};` (trailing field — checker tiles across a [0,1]-param face). `albedo` is the checker's base color.

### 3.2 Lowering (`bridge/src/aleph.lowering`)
- **`MaterialParams` is DUAL-DEFINED** — fully defined in BOTH `:lower` (lower.cppm:60) AND `:lowered` (lowered.cppm:50), both re-exported by the umbrella and imported by `:incremental`. Add `math::f32 uv_scale{4.0f};` as the trailing field to **BOTH copies, keeping them token-identical** (adding to only one is a hard gcc-16 module error — empirically confirmed: "failed to read compiled module cluster: Bad file data"). The same dual-definition applies to `LoweredEntity/LoweredCamera/LoweredScene` — touch both copies of any IR struct you change. `to_params` (lower.cppm:166) appends `, m.uv_scale` as the 6th positional arg; the positional `MaterialParams{...}` init at lower.cppm:314 stays valid (uv_scale defaults).
- **Two exhaustive `switch (m.kind)` over `types::MaterialKind` need a `TexturedLambertian` arm** (4 explicit cases, no `default` → `-Wswitch` warning; no `-Werror`, so non-fatal but a regression to fix):
  1. `build.cppm` `add_material:80` → `case TexturedLambertian: return scene_add_textured_lambertian(s, m.albedo, {m.uv_scale, m.uv_scale});` **(requires §3.3 first — the scene-add gains an albedo param + drops tex_id).**
  2. `apps/aleph_lower_demo/main.cpp:318` (its own `add_material` copy) → add the same arm (or a `default:`).
  (No other exhaustive `types::MaterialKind` switch exists: `to_params` doesn't switch on kind; `build_sw` `shade_face` is an if-chain with a default — §3.4.)

### 3.3 Scene + PT (`render/src/aleph.scene`, `render/src/aleph.render.rt`)
- `TexturedLambertianSoA` (material_soa.cppm): add `std::vector<math::Vec3> albedo;`. `textured_lambertian_append(s, albedo, tex_id, uv_scale)` pushes albedo too (tex_id stays a **vestigial dummy `0`** — procedural, no image; nothing else reads it). `scene_add_textured_lambertian(s, albedo, uv_scale)` (drops the tex_id param, supplies `0` internally).
- `sample_textured_albedo` (rt-material.cppm) — replace the grey stub with the analytic checker:
```cpp
const Vec2 sc = s.tex_lamb.uv_scale[mat_idx];
const Vec3 a  = s.tex_lamb.albedo[mat_idx];
const int  cu = static_cast<int>(std::floor(u * sc.x));
const int  cv = static_cast<int>(std::floor(v * sc.y));
return a * (((cu ^ cv) & 1) ? kCheckerHi : kCheckerLo);    // 1.0 / (128/255)
```
(`kCheckerHi/Lo` as `constexpr f32` in render.rt; pure-f32, deterministic. Both scatter (rt-material.cppm:80) and NEE (rt-integrator.cppm:79) call sites pass raw `rec.u/v` — uv_scale is applied INSIDE the sampler.)
- **Migrate the existing test** `tests/scene/test_material_soa.cpp` (it calls the OLD `textured_lambertian_append(t, 7u, …)` and asserts `tex_id[0]==7u`) to the new `(albedo, tex_id, uv_scale)` append + assert `albedo[0]`.

### 3.4 Raster (`bridge/.../aleph.lowering-build_sw.cppm`)
- **`shade_face`:** `TexturedLambertian` shades like **Lambertian** (the texture is applied by `f.tex×vcol` at raster time, not in the bake). The kind branch is an if-chain (Metal/Dielectric special, else default) — `TexturedLambertian` falls into the **default (Lambertian/Emissive)** branch automatically; confirm no explicit enumeration drops it. (`vcol = hadamard(albedo, amb)*ao + self_emit` as today — no change.)
- **`push_tri` gains UVs + a tex fn:** `push_tri(out, a,b,c, ca,cb,cc, uva,uvb,uvc, TexSampleFn fn, source)` — set `f.uvs={uva,uvb,uvc,uvc}`, `f.tex=fn`. **All 5 callers** updated (emit_quad ×2, emit_tri ×1, emit_sphere ×2).
- **`emit_quad`:** per corner `P(i,j)` bake `uv=(i/Nu, j/Nv)·uv_scale` (UNCONDITIONALLY — `tex_white` ignores uv for Lambertian quads, so this is harmless and keeps one code path). Select `fn = (mat.kind==TexturedLambertian && !phi) ? &tex_checker_uv : &tex_white`. Pass the 3 corner UVs to each `push_tri`.
- **`emit_sphere` / `emit_tri`:** pass `uv={0,0}` + `fn=&tex_white` to `push_tri` (spheres/tris are NOT textured this slice — §1). (The signature change is mechanical; the values are inert.)
- **φ guard (structural):** the `fn` selection forces `&tex_white` when `phi != null` — so a textured φ-entity is NEVER checker-multiplied (rast does `tex×vcol`; without this a φ floor would render `checker×colormap`). vcol is unchanged → the φ determinism tests stay byte-identical. (Moot for today's untextured lattice, but §3.5 textures the editor floor which `run_live` can drive with φ — this makes the invariant structural, not scene-dependent.)
- `tex_checker_uv` lives in `render.sw-scene_rt.cppm` beside `tex_checker` (a captureless `TexSampleFn`; `uv_scale` is baked into the UVs, not captured). `rast_scan` passes the interpolated uv to `tex(u,v)` with no wrap/clamp (these faces have `lightmap_id=0xFFFFFFFF` → the lightmap branch is skipped) — multi-tile uv (up to uv_scale) works.

### 3.5 Subject
Make the editor floor (`build_initial_graph`, apps/aleph_edit/main.cpp) `MaterialKind::TexturedLambertian` (a neutral tan/grey albedo, `uv_scale` ~4–8 tiles across the 8×8 floor). Both the raster preview and a path-trace of the scene then show the aligned checker.

## 4. Determinism
The checker is a pure-f32 function of (u,v); UVs are fixed per tessellation; no RNG. Raster: same `LoweredScene` ⇒ byte-identical `vcol` AND `f.uvs` (the `same_face` oracle compares `verts/uvs/vcol` — it does NOT compare the `f.tex` function pointer; the `f.tex==&tex_checker_uv` check is a SEPARATE assertion). PT: `sample_textured_albedo` is deterministic. **φ invariant:** φ overrides `vcol` but the §3.4 guard forces `f.tex=&tex_white` when `phi!=null`, so a φ face is never checker-multiplied — `vcol` byte-identical, φ tests unaffected. `--wave` lattice is all-Lambertian regardless.

## 5. Testing
**Raster (`tests/edit/test_build_sw.cpp`):**
- **UVs baked + checker fn for a textured quad:** a `TexturedLambertian` floor quad → faces carry the `(i/Nu,j/Nv)·uv_scale` grid (a non-zero UV exists) AND `f.tex == &tex_checker_uv`. A Lambertian quad keeps `f.tex == &tex_white` (its baked UVs are *ignored* by `tex_white` — do NOT assert zeroed UVs; the bake is unconditional).
- **Checker alternates:** `tex_checker_uv(0.5,0.5) != tex_checker_uv(1.5,0.5)` (adjacent), `== tex_checker_uv(1.5,1.5)` (diagonal); pin the two ARGB levels (`0xFFFFFFFF` / `0xFF808080`).
- **4c-i Lambert golden + `same_face` still hold** (TexturedLambertian is a NEW kind; existing Lambertian faces byte-identical — the golden compares `vcol` only, unaffected by baked UVs).
- **φ guard:** a textured quad built with a φ value → `f.tex == &tex_white` and `vcol` == the colormap (the existing φ test still passes).
**Scene/PT (`tests/scene/test_material_soa.cpp`, `tests/render/test_material_scatter.cpp`):**
- **SoA migration** (required edit): `test_material_soa.cpp` TexturedLambertian case → new `(albedo, tex_id, uv_scale)` append; assert `albedo[0]` + `tex_id[0]` + `uv_scale[0]`.
- **`sample_textured_albedo` checker** (in `test_material_scatter.cpp`): for `scene_add_textured_lambertian(albedo, {sx,sy})`, assert it returns `albedo·kCheckerHi` on a HI cell and `albedo·kCheckerLo` on a LO cell; adjacent tiles differ; deterministic. **AND the scatter() arm:** a `TexturedLambertian` hit's `scatter()->attenuation == sample_textured_albedo(...)` (the actual bounce albedo, mirroring the Lambertian scatter test).
**Graph (`tests/graph/test_attribute.cpp`):** `CHECK(static_cast<int>(MaterialKind::TexturedLambertian) == 4);` (mirrors the scene-side static_assert at test_handle32.cpp:32 — pins the cross-layer 1:1 mapping).
**Parity (the key oracle, no rendering):** for a floor quad, at a tessellation vertex the PT `hit_quad`'s `(α,β)` equals the raster's baked `(i/Nu,j/Nv)` **bit-for-bit** (`Vec2 operator==`); assert that AND `cell(α·sc) == cell(baked·sc)`. (Do NOT sample mid-cell points where `floor()` can straddle a boundary — the vertex identity IS the contract.)
**Visual:** before/after — editor floor flat vs checkered (raster) → `docs/superpowers/artifacts/2026-06-06-texture-checker-before-after.png`; AND a raster-vs-PT contact sheet of the textured floor (`visual_review.sh`) showing the tiles ALIGN.

## 6. Cost
Raster: the checker is a few int ops per pixel in the already-live `rast_scan` path — negligible. PT: two `floor`s — negligible. Bake: UV arithmetic in the existing emit loops. No new allocation beyond the SoA `albedo` vector.

## 7. Scope boundary (YAGNI)
**In:** a procedural checker `TexturedLambertian` through graph→lowering→both renderers; real UVs on the bake path for the **floor quad** (exact parity); a textured floor subject; raster+scene+PT+graph+parity tests. **Out (hooks kept):**
- *Textured spheres/tris* — the sphere has a longitude u-seam (needs duplicated wrap-column vertices or per-pixel UV) and the PT gives tris no UV; both stay `tex_white`. A seam-only sphere fix is a clean future add.
- *Image textures* — the `tex_id`→`Scene::textures` image path stays vestigial (dummy `tex_id=0`); procedural-only, deterministic, no asset loading. The image hook remains for a future `aleph_rt` 'earth' scene.
- *Other procedural patterns* (brick/wood), *normal maps / mipmapping / multiple textures* — one checker closes the gap.
`kCheckerHi/Lo` (= byte/255), the default `uv_scale`, and the LO ARGB byte are `constexpr`/material tunables (single source of truth: the LO byte).
