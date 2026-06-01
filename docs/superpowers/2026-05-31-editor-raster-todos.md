# TODOs — editor raster / hybrid view (Phase 6, found 2026-05-31)

Found while running the live `aleph_edit` (interactive, SDL). **The path-trace (idle) view is correct** (clean blue sphere, lit floor, soft shadow) → the graph + lowering + path tracer are fine. **The bugs are isolated to the raster navigation backend** (`build_sw_scene` + `aleph.render.sw`), i.e. what you see *while moving*. The hybrid raster↔path-trace switch itself is by-design; it just looks bad because the raster view is broken.

Initial scene: ENTITIES 8, LIGHTS 3, FACES 2690.

## T1 — [BUG, high] Raster sphere shows red triangles through the surface
Symptom: in raster (navigation) mode the tessellated UV-sphere shows scattered **red triangles** over the blue surface (img2); path-trace (img1) is fully correct.
Diagnosis so far:
- Color in `render.sw` is a per-pixel `TexSampleFn`; `build_sw_scene` packs the entity **albedo into the face UVs** and decodes via a captureless `tex_albedo`. Suspected: a **subset of sphere faces get wrong/unset UVs** (e.g. the second triangle of each cell, or seam/pole cells, or the degenerate-quad `{0,2,3}` path) → `tex_albedo` decodes garbage → red. (Material is blue everywhere; red is not a material — it's a decode artifact.)
- `render.sw` has a depth buffer + a `dist_sq` (z/w) face sort but **no back-face culling** — back faces of the sphere are rasterized (depth should hide them, but it may compound the artifact / z-fighting at the silhouette).
Fix plan: (a) verify the UV-albedo packing is applied to **every** sphere face (both per-cell triangles, poles, seam) and decodes bit-exact; (b) add **back-face culling** in `render.sw` (signed screen-space area / normal·view) — also a perf win; (c) confirm consistent triangle **winding** in the sphere tessellation.
Files: `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm` (sphere tessellation + UV packing), `render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm` (+ `:rast_scan`, `tex_albedo`).
Repro: `./build-release/apps/aleph_edit/aleph_edit`, orbit-drag. (Headless raster PPMs at `/tmp/edit_demo/*_raster.ppm` also show it.)

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
