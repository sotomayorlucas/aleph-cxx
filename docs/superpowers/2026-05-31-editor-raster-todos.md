# TODOs — editor raster / hybrid view (Phase 6, found 2026-05-31)

Found while running the live `aleph_edit` (interactive, SDL). **The path-trace (idle) view is correct** (clean blue sphere, lit floor, soft shadow) → the graph + lowering + path tracer are fine. **The bugs are isolated to the raster navigation backend** (`build_sw_scene` + `aleph.render.sw`), i.e. what you see *while moving*. The hybrid raster↔path-trace switch itself is by-design; it just looks bad because the raster view is broken.

Initial scene: ENTITIES 8, LIGHTS 3, FACES 2690.

## T1 — [BUG, high] Raster sphere shows red triangles through the surface — ✅ FIXED (2026-05-31)
Symptom: in raster (navigation) mode the tessellated UV-sphere shows scattered **red triangles** over the blue surface (img2); path-trace (img1) is fully correct.
ROOT CAUSE (confirmed): `build_sw_scene` packed the entity **albedo into the face UVs** as exact-integer floats up to 65535 and decoded them per pixel via a captureless `tex_albedo`. Although all four UVs of a face were identical (zero gradient in theory), the rasterizer's perspective-correct interpolation computes the UV per pixel as `u_w_acc / inv_w_acc` accumulated in 16-px subspans — tiny f32 rounding on sliver/grazing sphere triangles perturbed the interpolated value by a few units, which flipped high bits in the ARGB decode → stray colours (notably red, the high R byte). It was a decode artifact, not a material. (Not the placeholder-lightmap path: the editor's `flat_raster` already zeroed `lightmap_id`, so lightmap modulation was off; the codec was the sole colour source.)
FIX: give `render::sw::Face` a flat `albedo` Vec3 tint that the rasterizer modulates onto the per-pixel texel; `build_sw` sets `Face::albedo = material.albedo` + pairs it with a constant-white texture `tex_white` (so colour = white·albedo = albedo) and emits NO lightmaps (`lightmap_id = 0xFFFFFFFF`). A flat per-face colour is interpolation-proof — no large UVs, no per-pixel decode. Verified: headless raster PPMs now show clean solid spheres (red + blue) that match the path-trace reference; ctest 19/19; release-strict 0 warnings.
Files changed: `render/.../aleph.render.sw-scene_rt.cppm` (Face += albedo), `aleph.render.sw-rast_scan.cppm` (rast_scan_textured += albedo param + modulation), `aleph.render.sw-rasterize.cppm` (pass face.albedo), `bridge/.../aleph.lowering-build_sw.cppm` (tex_white + flat albedo, drop UV codec + placeholder lightmaps), `tests/render/test_sw_rasterize.cpp` (call-site arg).
NOTE: back-face culling (T4) was NOT needed — the existing `if (signed_area < 0) return;` in rast_scan already culls; the artifact was purely the UV codec.

## T2 — [polish] Raster floor is flat/unlit — ✅ FIXED (2026-05-31)
The raster floor was flat gray (img2) vs the lit floor in path-trace (img1).
FIX: bake a cheap FLAT LAMBERT shade into each `Face::albedo` in `build_sw`
(the bridge is the one place graph lights + render.sw meet — render.sw stays
graph-free). `shade_face` = `albedo·kAmbient + self_emit + Σ_lights albedo⊙emit·max(0,N·L)·atten·kLightScale`,
`atten = 1/(1+kFall·dist²)`. Spheres use their exact outward normal (centroid−centre,
one-sided → real light/dark terminator); quads/tris use the geometric cross-product
normal two-sided (|N·L|) so a floor lit from either winding reads as lit. Lights are
`LoweredScene::lights` (centre = geometry centre, radiance = `material.emit`). Tuned
(kAmbient 0.20, kLightScale 0.28, kFall 0.08) so the floor reads ≈ the path-trace
average (verified: raster floor (189,189,192) vs PT (187–207)) → exposure now matches,
which also serves T3. NOTE: the floor is ONE quad = 2 triangles, so flat per-face
shading makes it uniformly lit (no gradient); a gradient would need tessellation,
which violates the SPEC `QuadLocal→2 faces` invariant (and `test_build_sw`), or
per-vertex (Gouraud) normals in the rasterizer — deliberately out of scope. Spheres
show visible facets (12×16 flat-shaded) — also inherent to flat shading. Pure f32 over
lights-in-order → deterministic (pinned by extending `test_build_sw` same_face to also
compare `Face::albedo`). Files: `bridge/src/aleph.lowering/aleph.lowering-build_sw.cppm`.

## T3 — [polish] Hybrid switch is jarring — ✅ FIXED (2026-05-31)
The jump between raster (navigating) and path-trace (idle) was an abrupt hard cut.
FIX (`apps/aleph_edit/main.cpp` run_live): a LINEAR-space crossfade on every
raster↔path-trace mode switch. `presented` always holds the last shown frame; on a
mode change we snapshot it into `fade_from` and ramp `alpha` 0→1 over `kFadeMs`=180,
presenting `lerp(fade_from, src, alpha)` then tonemapping. Steady-state (no mode
change) presents `src` verbatim → crisp + responsive while orbiting; the fade costs
only a per-pixel lerp during the ~180ms ramp, in BOTH directions. T2 already brought
the raster exposure close to the path trace, so the crossfade has little brightness gap
to hide. Also fixed a latent bug: when idle && converged (`pt_samples>=kMaxSpp`) the old
code flipped BACK to raster; now it holds the converged path-trace mean. And dropped the
redundant `flat_raster()` copy (build_sw faces already carry `lightmap_id=0xFFFFFFFF`),
rasterizing `controller.raster_scene()` directly in both run_headless and run_live.

## T4 — [note] render.sw lacks back-face culling
`render/src/aleph.render.sw/aleph.render.sw-rasterize.cppm` sorts by `dist_sq` + uses a depth buffer but never culls back faces. Adding culling helps T1 and is a perf win on closed meshes (spheres/cubes). Keep an opt-out for double-sided faces (quads/floor) if needed.
NOTE (2026-06-05): there IS a screen-space cull (`if (signed_area < 0) return;` in rast_scan); the "depth buffer" was the bug — see T5.

## T5 — [BUG, critical] Raster buried any object near the camera target — ✅ FIXED (2026-06-05)
Symptom: a sphere resting on the floor showed only its TOP CAP in raster (nav) mode; the path-trace of the SAME scene+camera showed the full sphere grounded with a contact shadow. Off-centre objects (a second sphere) looked fine — only objects near the look-at target sank.
DIAGNOSIS (headless render + depth dump + numeric reprojection): the raster MVP (`perspective*look_at`) and the PT camera (`make_camera`) project identically (sphere top matched to the pixel), so NOT a camera bug; the floor far edge projected ~50px too high, occluding the sphere.
ROOT CAUSE: hidden-surface removal was a coverage / C-buffer (`SpanBuffer`) driven by a front-to-back painter sort on **face-centre depth**. The floor quad's near triangle (centroid in front of the sphere) drew first, marked the sphere's lower-half pixels covered, and the sphere's spans were then SKIPPED before any depth compare. Face-centre order is simply wrong per-pixel for a large triangle. The passed-in depth buffer was unused (`(void)depth`).
FIX: real per-pixel z-test in `rast_scan` using **1/w** (view-linear) depth — the value already interpolated for perspective correction, exactly affine in screen space and free of the NDC-z near-plane precision crunch (which collapsed everything past a few units to ~1.0 and z-fought). Nearer = larger 1/w; buffers clear to 0 (far). `SpanBuffer` dropped its coverage HSR → thin span clamp. Callers (aleph_edit headless+live, aleph_sw) clear the z-buffer per frame. Files: `aleph.render.sw-rast_scan.cppm`, `-span_buffer.cppm`, `-rasterize.cppm`, `apps/aleph_{edit,sw}/main.cpp`, `tests/render/test_sw_{rasterize,rasterize_full,span_buffer}.cpp`.

## T6 — [polish] Faceted spheres + flat floor → GOURAUD shading — ✅ FIXED (2026-06-05)
The flat per-face shade made spheres visibly faceted (12×16) and the floor one flat tone.
FIX: `Face` now carries a per-vertex `vcol[4]` (was a single flat `albedo`); `clip`/`ScreenVert` thread it through near-plane clipping, and `rast_scan` interpolates it (affine Gouraud, 3 channels) onto the white texel. `build_sw` bakes the Lambert shade PER VERTEX: spheres use each vertex's exact outward normal (smooth, no facets); quads/tris share the flat face normal but, shaded at each corner, pick up a smooth distance-falloff gradient across the floor. Tuned `kAmbient` 0.20→0.45 (stands in for the bright sky dome the tracer integrates) and `kLightScale` 0.28→0.50 so the raster floor (~174) approaches the PT floor (~204) without washing out the sphere terminators. Pinned deterministic by `test_build_sw` comparing `vcol`. File: `aleph.lowering-build_sw.cppm`.

## Method — `tools/visual_review.sh`
Render the headless Op script and tile RASTER (top) vs PATH-TRACE (bottom) into one labeled contact sheet (`_contact.png`) for eyeballing visual progress; `ALEPH_DUMP_DEPTH=1` adds a depth sheet (1/w, near=bright) for occlusion debugging.

---
Note: all of the above are **raster-navigation-only**; the authoritative graph→lower→path-trace pipeline is correct and verified (ctest 19/19). These are visual-quality issues in the fast preview, not correctness of the engine.
