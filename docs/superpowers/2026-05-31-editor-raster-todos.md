# TODOs — editor raster / hybrid view (Phase 6, found 2026-05-31)

Found while running the live `aleph_edit` (interactive, SDL). **The path-trace (idle) view is correct** (clean blue sphere, lit floor, soft shadow) → the graph + lowering + path tracer are fine. **The bugs are isolated to the raster navigation backend** (`build_sw_scene` + `aleph.render.sw`), i.e. what you see *while moving*. The hybrid raster↔path-trace switch itself is by-design; it just looks bad because the raster view is broken.

Initial scene: ENTITIES 8, LIGHTS 3, FACES 2690.

## T1 — [BUG, high] Raster sphere shows red triangles through the surface — ✅ FIXED (2026-05-31)
Symptom: in raster (navigation) mode the tessellated UV-sphere shows scattered **red triangles** over the blue surface (img2); path-trace (img1) is fully correct.
ROOT CAUSE (confirmed): `build_sw_scene` packed the entity **albedo into the face UVs** as exact-integer floats up to 65535 and decoded them per pixel via a captureless `tex_albedo`. Although all four UVs of a face were identical (zero gradient in theory), the rasterizer's perspective-correct interpolation computes the UV per pixel as `u_w_acc / inv_w_acc` accumulated in 16-px subspans — tiny f32 rounding on sliver/grazing sphere triangles perturbed the interpolated value by a few units, which flipped high bits in the ARGB decode → stray colours (notably red, the high R byte). It was a decode artifact, not a material. (Not the placeholder-lightmap path: the editor's `flat_raster` already zeroed `lightmap_id`, so lightmap modulation was off; the codec was the sole colour source.)
FIX: give `render::sw::Face` a flat `albedo` Vec3 tint that the rasterizer modulates onto the per-pixel texel; `build_sw` sets `Face::albedo = material.albedo` + pairs it with a constant-white texture `tex_white` (so colour = white·albedo = albedo) and emits NO lightmaps (`lightmap_id = 0xFFFFFFFF`). A flat per-face colour is interpolation-proof — no large UVs, no per-pixel decode. Verified: headless raster PPMs now show clean solid spheres (red + blue) that match the path-trace reference; ctest 19/19; release-strict 0 warnings.
Files changed: `render/.../aleph.render.sw-scene_rt.cppm` (Face += albedo), `aleph.render.sw-rast_scan.cppm` (rast_scan_textured += albedo param + modulation), `aleph.render.sw-rasterize.cppm` (pass face.albedo), `bridge/.../aleph.lowering-build_sw.cppm` (tex_white + flat albedo, drop UV codec + placeholder lightmaps), `tests/render/test_sw_rasterize.cpp` (call-site arg).
NOTE: back-face culling (T4) was NOT needed — the existing `if (signed_area < 0) return;` in rast_scan already culls; the artifact was purely the UV codec.

## T2 — [polish] Raster floor is flat/unlit
The raster floor is flat gray (img2) vs the lit, soft-shadowed floor in path-trace (img1). `render.sw` does minimal shading (no light response on the floor here). Add at least N·L flat shading (or a lightmap pass) so the raster preview is recognizable as the same scene.
Files: `render/src/aleph.render.sw/*` (shading), possibly `build_sw_scene` (emit normals/light info).

## T3 — [polish] Hybrid switch is jarring
The jump between the flat/broken raster (navigating) and the lit path-trace (idle) is abrupt. After T1+T2, reduce the discontinuity: match exposure, keep the last path-trace frame during a brief grace period, or crossfade raster→path-trace.
Files: `apps/aleph_edit/main.cpp` (mode-switch loop).

## T4 — [note] render.sw lacks back-face culling
`render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm` sorts by `dist_sq` + uses a depth buffer but never culls back faces. Adding culling helps T1 and is a perf win on closed meshes (spheres/cubes). Keep an opt-out for double-sided faces (quads/floor) if needed.

---
Note: all of the above are **raster-navigation-only**; the authoritative graph→lower→path-trace pipeline is correct and verified (ctest 19/19). These are visual-quality issues in the fast preview, not correctness of the engine.
