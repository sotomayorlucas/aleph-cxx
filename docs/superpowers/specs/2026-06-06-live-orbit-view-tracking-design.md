# Design Spec — Live-Orbit View Tracking for Material Shading (visual slice 4c-i-b)

**Goal:** make the view-dependent **Metal/Dielectric** raster shading (slice 4c-i: chrome reflection direction, specular highlight, glass Fresnel) **track the camera as the user orbits** the live editor — instead of freezing at the last-baked pose until the next edit. Today `sw_` (the baked raster vcol) is re-baked only on `apply(Op)`/wave-`step`; the live orbit moves `cam_` + the raster MVP, but the baked Metal/Dielectric vcol stays pinned to the eye at the last bake. Date 2026-06-06 · Status: DRAFT. Follow-up to 4c-i (the deferred §7 item); the LAST visual follow-up.

Context (verified): `EditorController::rebuild_backends_from_prev` (controller.cppm:518) bakes `sw_ = build_sw_scene(prev_, cam_.look_from(), …)` (view-dependent) AND `render_ = build_render_scene(prev_)` + `prim_source_` (both **view-INDEPENDENT** — the PT integrates the view via rays; the SoA/BVH don't depend on the eye). The shell (`apps/aleph_edit/main.cpp` `run_live`) mutates `controller.camera()` on orbit (`.orbit(dx,dy)`, ~742) + zoom (`.zoom(...)`, ~747) inside the poll loop, and every frame rasterizes `controller.raster_scene()` (= `sw_`) through `orbit_mvp(controller.camera(), …)`. So the MVP tracks the orbit but the baked vcol does not. `run_headless`/`run_wave` set a fixed pose + already call `rebake_view()` once (4c-i) — they don't orbit. Only `build_sw`/`EditorController`/the shell loop change; the PT is untouched.

## 1. Approach
Two pieces — a cheap controller entry point + a shell-side throttle:
- **`sw_`-only re-bake** (`EditorController::rebake_view_sw()`): re-bake ONLY `sw_` at the current `cam_.look_from()`, leaving `render_`/BVH + `prim_source_` untouched (they're view-independent and only consumed when the editor goes idle/path-trace — re-running `build_render_scene`+`scene_build_bvh` per orbit tick is pure waste). This is the existing `sw_`-build half of `rebuild_backends_from_prev`, factored out.
- **Gate to view-dependent scenes** (`has_view_dependent_material()`): only Metal/Dielectric vcol depends on the eye; Lambertian/TexturedLambertian/Emissive vcol is view-independent (no `V`). If the scene has no Metal/Dielectric entity, orbiting needs NO re-bake at all (the cached `sw_` is already correct). So the shell skips the re-bake unless a view-dependent material is present.
- **Shell throttle (wall-clock, SHELL-ONLY so the controller stays deterministic):** on orbit/zoom set `view_dirty=true`; in the render loop, if `view_dirty && controller.has_view_dependent_material() && (now − last_rebake) ≥ kViewRebakeMs`, call `controller.rebake_view_sw()`, clear `view_dirty`, reset `last_rebake`. `kViewRebakeMs≈80` (~12 Hz) keeps the chrome tracking smoothly without a re-bake every frame. (A still-set `view_dirty` after the orbit stops re-bakes the final pose within one interval → converges to the exact view.)

**Determinism:** the controller gains NO wall-clock — `rebake_view_sw()` is a pure re-bake (byte-identical to `build_sw_scene(prev_, cam_.look_from(), …)`); the throttle/`view_dirty`/`steady_clock` live entirely in `run_live` (the interactive SDL path — never headless, never tested). `run_headless`/`run_wave` are unchanged (no orbit; the single 4c-i `rebake_view()` at the framed pose stands). The `same_face`/golden oracles are unaffected.

## 2. Components

### 2.1 `EditorController` (controller.cppm)
- Factor the φ-gather (currently inline in `rebuild_backends_from_prev`:528-533) into `std::vector<double> gather_phi_entity() const` (returns the per-entity φ aligned to `prev_.entities`, or empty when `!sim_enabled_`).
- **`void rebake_view_sw()`** — the `sw_`-only re-bake:
```cpp
void rebake_view_sw() {
    if (sim_enabled_) { const auto phi = gather_phi_entity();
                        sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from(), &phi); }
    else              { sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from()); }
}
```
- **`rebuild_backends_from_prev`** calls `rebake_view_sw()` for the `sw_` part, then does `render_`/`prim_source_` as today (so the full path is unchanged behaviourally; `rebake_view()` still = `rebuild_backends_from_prev()`).
- **`[[nodiscard]] bool has_view_dependent_material() const`** — `true` iff any `prev_.entities[i].material.kind` is `Metal` or `Dielectric` (the only eye-dependent shades). (TexturedLambertian/Lambertian/Emissive → view-independent.)

### 2.2 Shell `run_live` (apps/aleph_edit/main.cpp)
- A `bool view_dirty = false;` + a `steady_clock::time_point last_rebake` near the render-loop state.
- After the `.orbit(...)` (~742) and `.zoom(...)` (~747) mutations: `view_dirty = true;`.
- In the render block, BEFORE `rasterize(...)`:
```cpp
if (view_dirty && controller.has_view_dependent_material()) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_rebake >= std::chrono::milliseconds(kViewRebakeMs)) {
        controller.rebake_view_sw();
        last_rebake = now;
        view_dirty = false;
    }
}
```
(`kViewRebakeMs` a shell-local `constexpr int = 80`. `<chrono>` include. The wave-demo branch of `run_live` steps+rebakes every frame already, so the orbit re-bake is mainly the non-wave path — but the guard is harmless in both.)

## 3. Determinism / scope
The controller is clock-free and deterministic; `rebake_view_sw()` is exactly the `sw_` half of the existing bake (byte-identical at the same eye). All wall-clock/throttle state is in `run_live` (interactive, untested, non-headless). Headless/wave PPMs + `same_face` + the 4c-i Lambert golden are unaffected. `--wave` byte-identity holds (wave φ-skips shade; `rebake_view_sw` φ-path matches `rebuild_backends_from_prev`'s).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions beyond the existing bake. `has_view_dependent_material` is a const scan. Empty scene → false → no re-bake.

## 5. Testing
**Controller (`tests/edit/test_controller.cpp` or `test_mv_controller.cpp`):**
- **`rebake_view_sw` == the full path's `sw_`:** after an edit, capture `sw_` (via `raster_scene()`); call `rebake_view_sw()`; assert the `sw_` faces are byte-identical (`same_face`-style) to `build_sw_scene(prev_, cam_.look_from())` — i.e. `rebake_view_sw` reproduces the sw bake exactly (no divergence from the factored-out path).
- **`rebake_view_sw` leaves `render_`/`prim_source_` untouched:** mutate the orbit camera, call `rebake_view_sw()`, assert `render_scene()`'s primitive count / `prim_source_` are unchanged from before (it only re-baked `sw_`). (If `render_` identity is hard to assert, assert `prim_source_` is byte-unchanged — it's rebuilt only by the full path.)
- **`has_view_dependent_material`:** a scene with a Metal (or Dielectric) entity → `true`; an all-Lambertian/Textured scene → `false`.
- **Orbit changes Metal vcol but not Lambertian:** build a scene with a Metal sphere + a Lambertian sphere + a camera; bake; record a Metal face vcol and a Lambertian face vcol; move `cam_` (orbit); `rebake_view_sw()`; assert the Metal face vcol CHANGED and the Lambertian face vcol is byte-IDENTICAL (proves view-dependence is isolated to Metal/Dielectric, and the gate's rationale).
- (The shell throttle is app-loop glue — not unit-tested; note it.)
**Visual:** two raster frames of the editor scene at DIFFERENT orbit yaw, side-by-side, showing the chrome highlight / sky-reflection has MOVED with the camera (vs the 4c-i frozen behaviour) → `docs/superpowers/artifacts/2026-06-06-live-orbit-tracking.png`. (Headless can't orbit interactively; render two frames by setting two camera poses + `rebake_view_sw()` each, or note it's an interactive behaviour demonstrated by the 2-pose montage.)

## 6. Cost / when it runs
`rebake_view_sw()` re-runs the full `sw_` bake (incl. the view-INDEPENDENT AO + shadows — ~hundreds of K segment tests for the editor scene, single-threaded, a few ms) but NOT `render_`/BVH. Gated by `has_view_dependent_material()` (so all-Lambertian scenes never re-bake on orbit) + throttled to `kViewRebakeMs` (~12 Hz). A few-ms bake at 12 Hz is <10% of the frame budget — acceptable for the editor scene. **§7 hook:** if it's janky on a heavier scene, cache the view-independent per-vertex term (ambient+AO+diffuse+shadows) once and recompute only the Metal/Dielectric eye-dependent term on orbit (the "view-independent/dependent split") — a `build_sw` refactor, not built here.

## 7. Scope boundary (YAGNI)
**In:** `rebake_view_sw()` (sw-only, view-eye), `has_view_dependent_material()` gate, the shell throttle so chrome/glass track the live orbit. **Out (hooks kept):**
- *View-independent/dependent vcol split* — recompute only the eye-dependent term on orbit (avoid re-AO); the throttle + gate make the full sw re-bake acceptable at editor sizes for now.
- *Per-frame (un-throttled) re-bake* — 12 Hz is smooth enough; per-frame would re-bake AO 60×/s for no visible gain.
- *Re-baking the PT `render_`/BVH on orbit* — explicitly NOT done (view-independent; only consumed when idle).
- *Threading the bake* — single-threaded is fine at editor sizes; a thread pool is a separate perf change.
`kViewRebakeMs` is a shell `constexpr` tunable.
