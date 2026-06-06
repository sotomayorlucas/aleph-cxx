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
- Factor the φ-gather (currently inline in `rebuild_backends_from_prev`:528-533) into `std::vector<double> gather_phi_entity() const` — **pin the body byte-identically** (build_sw_scene indexes φ by entity `i`, so the three properties are load-bearing): size to `prev_.entities.size()` (NOT `u_.order.size()`); default each to `0.0` (the "φ=0 neutral white" contract for entities with no field entry); first-match-wins. I.e. `std::vector<double> phi(prev_.entities.size(), 0.0); for (i) { src = prev_.entities[i].source; for (j in u_.order) if (u_.order[j]==src) { phi[i]=u_.data[j]; break; } } return phi;`.
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
**Reuse the shell's existing `u32` ms clock — `win.ticks_ms()` — NOT a new `std::chrono`.** `run_live` already drives all timing off it (`last_input_ms`, `kIdleMs`, fade timing) and already computes `const u32 now = win.ticks_ms();` right before `rasterize` (~main.cpp:801). So:
- A `bool view_dirty = false;` + `aleph::math::u32 last_rebake_ms = 0;` (init `0`, NOT `now` — guarantees the first orbit re-bakes on the first throttled frame, closing the first-frame-lag hole) near the render-loop state. `constexpr aleph::math::u32 kViewRebakeMs = 80;` (shell-local).
- After the `.orbit(...)` (~742) and `.zoom(...)` (~747) mutations: `view_dirty = true;`.
- In the render block, using the already-computed `now` (BEFORE `rasterize`):
```cpp
if (view_dirty && controller.has_view_dependent_material()
        && (now - last_rebake_ms) >= kViewRebakeMs) {
    controller.rebake_view_sw();
    last_rebake_ms = now;
    view_dirty = false;
}
```
(No `<chrono>`; matches the codebase u32-ms idiom. The wave-demo branch's lattice is all-Lambertian → `has_view_dependent_material()==false` → the guard is fully gated off there, so it never double-rebakes against `step()`'s per-frame rebuild.)

## 3. Determinism / scope
The controller is clock-free and deterministic; `rebake_view_sw()` is exactly the `sw_` half of the existing bake (byte-identical at the same eye). All wall-clock/throttle state is in `run_live` (interactive, untested, non-headless). Headless/wave PPMs + `same_face` + the 4c-i Lambert golden are unaffected. `--wave` byte-identity holds (wave φ-skips shade; `rebake_view_sw` φ-path matches `rebuild_backends_from_prev`'s).

## 4. Error handling (`aleph_flags_isa`)
No allocation/exceptions beyond the existing bake. `has_view_dependent_material` is a const scan. Empty scene → false → no re-bake.

## 5. Testing
**Controller (`tests/edit/test_controller.cpp` or `test_mv_controller.cpp`).** Keep these as THIN integration checks of the new wiring — the per-pixel Metal/Dielectric/Lambert shading semantics are already pinned at the build_sw layer (`test_build_sw.cpp` make_material_scene + the metal/dielectric/golden oracles); do NOT restate shading thresholds here.
- **`rebake_view_sw` == the full path's `sw_` (sim-OFF):** the controller exposes the IR via `c.lowered()` (no public `prev_`). After an edit, assert `c.raster_scene()` faces are `same_face`-byte-identical to `build_sw_scene(c.lowered(), c.camera().look_from())` (the eye-explicit overload). (sim-ON routes through `gather_phi_entity()` and can't be reconstructed without enabling sim — cover the sim-OFF case.)
- **`rebake_view_sw` leaves `render_`/`prim_source_` untouched:** mutate the orbit camera, `rebake_view_sw()`, assert **`prim_source()` is byte-unchanged** (PRIMARY — it's rebuilt only by the full path, controller.cppm:545-549; `render_scene()` primitive identity is harder to diff).
- **`has_view_dependent_material`:** a Metal (or Dielectric) entity → `true`; an all-Lambertian/Textured scene → `false`.
- **Orbit threads the live eye (the genuinely new behaviour):** a scene with a Metal sphere + a Lambertian sphere + a camera; bake; record per-Metal-face and per-Lambertian-face vcol. Orbit by a **LARGE yaw** (`c.camera().orbit(400.0f, 0.0f)` ≈ 3.2 rad — yaw is unclamped, kOrbitSpeed=0.008) so the reflection moves unambiguously; `rebake_view_sw()`. Assert `max |vcol_new − vcol_old|` over ALL Metal faces exceeds a clear threshold (the eye reached the bake), AND every Lambertian face vcol is byte-IDENTICAL (`operator==` — the default branch never reads `V`). This proves the threading + the gate rationale, not the shading math.
- (The shell throttle is app-loop glue under `#if ALEPH_HAVE_SDL2` — not unit-tested; note it.)
**Visual:** two raster frames of the editor scene at DIFFERENT orbit yaw, side-by-side, showing the chrome highlight / sky-reflection has MOVED with the camera (vs the 4c-i frozen behaviour) → `docs/superpowers/artifacts/2026-06-06-live-orbit-tracking.png`. (Headless can't orbit interactively; render two frames by setting two camera poses + `rebake_view_sw()` each, or note it's an interactive behaviour demonstrated by the 2-pose montage.)

## 6. Cost / when it runs
`rebake_view_sw()` re-runs the full `sw_` bake (incl. the view-INDEPENDENT AO + shadows — ~hundreds of K segment tests for the editor scene, single-threaded, a few ms) but NOT `render_`/BVH. Gated by `has_view_dependent_material()` (so all-Lambertian scenes never re-bake on orbit) + throttled to `kViewRebakeMs` (~12 Hz). A few-ms bake at 12 Hz is <10% of the frame budget — acceptable for the editor scene. **§7 hook:** if it's janky on a heavier scene, cache the view-independent per-vertex term (ambient+AO+diffuse+shadows) once and recompute only the Metal/Dielectric eye-dependent term on orbit (the "view-independent/dependent split") — a `build_sw` refactor, not built here.

## 7. Scope boundary (YAGNI)
**In:** `rebake_view_sw()` (sw-only, view-eye), `has_view_dependent_material()` gate, the shell throttle so chrome/glass track the live orbit. **Out (hooks kept):**
- *View-independent/dependent vcol split* — recompute only the eye-dependent term on orbit (avoid re-AO); the throttle + gate make the full sw re-bake acceptable at editor sizes for now.
- *Per-frame (un-throttled) re-bake* — 12 Hz is smooth enough; per-frame would re-bake AO 60×/s for no visible gain.
- *Re-baking the PT `render_`/BVH on orbit* — explicitly NOT done (view-independent; only consumed when idle).
- *A rebake at the idle→path-trace transition* — NOT needed: the PT consumes `render_` (view-independent SoA/BVH, main.cpp:829) + the live orbit camera (`render_camera`, ~820), so a stale `sw_` never reaches the path-traced image; and `view_dirty` converges the final raster pose within `kViewRebakeMs` (80ms) ≪ `kIdleMs` (250ms before idle/PT kicks in).
- *Threading the bake* — single-threaded is fine at editor sizes; a thread pool is a separate perf change.
`kViewRebakeMs` is a shell `constexpr` tunable.
