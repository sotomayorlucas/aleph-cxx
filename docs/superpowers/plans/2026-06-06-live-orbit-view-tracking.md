# Live-Orbit View Tracking — Implementation Plan (visual slice 4c-i-b)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use `- [ ]`.

**Goal:** make Metal/Dielectric raster shading track the orbit camera in the live editor (chrome/glass currently freeze until the next edit). Add a `sw_`-only re-bake + a view-dependent gate + a shell throttle.

**Spec:** `docs/superpowers/specs/2026-06-06-live-orbit-view-tracking-design.md` (SPEC-READY — read §2/§5). **Conventions:** `cmake --build build-release && ctest --test-dir build-release`; `--test-case="edit:*"`; strict `cmake --build build-release-strict 2>&1 | grep "warning:" | grep -v '^ninja:' | wc -l` → 0.

**Confirmed accessors:** `camera()` mutable (controller.cppm:365), `raster_scene()` (368), `render_scene()` (371), `lowered()` (378), `prim_source()` (394); `win.ticks_ms()` → u32 (window.cppm:102), `const u32 now` at main.cpp:801.

---

## Task 1: controller — rebake_view_sw + gate + refactor + tests

**Files:** `bridge/src/aleph.edit/aleph.edit-controller.cppm`, `tests/edit/test_controller.cpp` (or `test_mv_controller.cpp`).

- [ ] **Step 1 — factor `gather_phi_entity()`** (private/`detail` helper on the controller). Extract the inline φ-gather (controller.cppm:528-533) VERBATIM (byte-identity matters — build_sw indexes φ by entity i):
```cpp
[[nodiscard]] std::vector<double> gather_phi_entity() const {
    std::vector<double> phi(prev_.entities.size(), 0.0);
    for (std::size_t i = 0; i < prev_.entities.size(); ++i) {
        const aleph::types::NodeId src = prev_.entities[i].source;
        for (std::size_t j = 0; j < u_.order.size(); ++j)
            if (u_.order[j] == src) { phi[i] = u_.data[j]; break; }
    }
    return phi;
}
```

- [ ] **Step 2 — `rebake_view_sw()`** (public, sw_-only, at the live orbit eye):
```cpp
// Re-bake ONLY sw_ at the current orbit eye (view-dependent Metal/Dielectric vcol
// tracks the camera). render_/BVH + prim_source_ are view-INDEPENDENT (the PT
// integrates the view via rays; only consumed when idle) — left untouched.
void rebake_view_sw() {
    if (sim_enabled_) { const std::vector<double> phi = gather_phi_entity();
                        sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from(), &phi); }
    else              { sw_ = aleph::lowering::build_sw_scene(prev_, cam_.look_from()); }
}
```

- [ ] **Step 3 — refactor `rebuild_backends_from_prev`** to call `rebake_view_sw()` for the sw_ part, then keep the render_/prim_source_ tail UNCHANGED (controller.cppm:541-549). Net behaviour identical (so `rebake_view()` = `rebuild_backends_from_prev()` still rebuilds both). Confirm `same_face`/wave/headless oracles unaffected.

- [ ] **Step 4 — `has_view_dependent_material()`** (public, const):
```cpp
// Only Metal/Dielectric vcol depends on the eye; Lambertian/TexturedLambertian/
// Emissive are view-independent (shade_face's default branch never reads V).
[[nodiscard]] bool has_view_dependent_material() const {
    for (const auto& e : prev_.entities)
        if (e.material.kind == aleph::types::MaterialKind::Metal ||
            e.material.kind == aleph::types::MaterialKind::Dielectric) return true;
    return false;
}
```

- [ ] **Step 5 — controller tests** (`test_controller.cpp`). THIN integration checks (shading semantics already pinned in test_build_sw):
  - **rebake_view_sw == full sw_ (sim-OFF):** build a controller (a scene via the existing test harness — reuse `make_two_mesh`/an editor-style scene), assert `raster_scene().faces` are `same_face`-byte-identical to `aleph::lowering::build_sw_scene(c.lowered(), c.camera().look_from())`.
  - **prim_source unchanged by rebake_view_sw:** capture `c.prim_source()` (copy), orbit `c.camera().orbit(400,0)`, `c.rebake_view_sw()`, assert `prim_source()` byte-unchanged (element-wise NodeId ==).
  - **has_view_dependent_material:** a Metal/Dielectric scene → true; all-Lambertian → false. (Construct via the controller's scene-build or a LoweredScene-direct path if the controller allows; else add a tiny graph with a Metal mesh.)
  - **orbit threads the eye:** scene with a Metal sphere + a Lambertian sphere + a camera; bake; record per-Metal-face + per-Lambertian-face vcol[0]; `c.camera().orbit(400.0f, 0.0f)`; `c.rebake_view_sw()`; assert `max |metal vcol_new − vcol_old|` over Metal faces > 0.05 (a clear move) AND every Lambertian face vcol byte-IDENTICAL (`operator==`). (Identify Metal vs Lambertian faces via `prim_source()` → the entity's material kind, or by NodeId.)

- [ ] **Step 6 — build + run + strict.** `--test-case="*"` for the edit suite + full `ctest` (wave/headless/same_face unaffected). Strict 0. **Commit** `feat(edit): rebake_view_sw (sw-only, orbit eye) + has_view_dependent_material`.

---

## Task 2: shell throttle + tracking artifact

**Files:** `apps/aleph_edit/main.cpp`.

- [ ] **Step 1 — throttle state** in `run_live` near the render-loop locals: `bool view_dirty = false;`, `aleph::math::u32 last_rebake_ms = 0;`, `constexpr aleph::math::u32 kViewRebakeMs = 80;`.

- [ ] **Step 2 — set dirty on gesture.** After `controller.camera().orbit(...)` (~742) AND `controller.camera().zoom(...)` (~747): `view_dirty = true;`.

- [ ] **Step 3 — throttled re-bake** in the render block, using the existing `const u32 now = win.ticks_ms();` (~801), BEFORE the `rasterize(...)` call:
```cpp
if (view_dirty && controller.has_view_dependent_material()
        && (now - last_rebake_ms) >= kViewRebakeMs) {
    controller.rebake_view_sw();
    last_rebake_ms = now;
    view_dirty = false;
}
```
(If `now` is computed after the rasterize in some branch, move the snippet to just after `now` is available and before the raster present. The wave branch's lattice is all-Lambertian → gate is false → no double-rebake.)

- [ ] **Step 4 — build + smoke.** `cmake --build build-release` clean; `--headless` still runs (the throttle is in `run_live` only, not headless). Full `ctest`; strict 0.

- [ ] **Step 5 — tracking artifact.** Two raster frames of the editor scene at DIFFERENT orbit yaw (set `controller.camera().orbit(...)` to pose A, `rebake_view_sw()`, render; then pose B, `rebake_view_sw()`, render), side-by-side → `docs/superpowers/artifacts/2026-06-06-live-orbit-tracking.png`. The chrome highlight / sky-reflection should have MOVED with the camera (vs the frozen 4c-i behaviour). (A small headless harness or a `visual_review.sh` extension; if interactive-only, render the 2 poses via a throwaway headless snippet that orbits + rebakes + dumps.) **Commit** `feat(edit): live-orbit view-tracking throttle + 2-pose tracking artifact`.

---

## Final verification
- [ ] `rebake_view_sw` reproduces the full path's `sw_` (sim-OFF) + leaves `prim_source_` untouched; `has_view_dependent_material` classifies; orbit moves Metal vcol but not Lambertian (large-yaw oracle).
- [ ] `ctest` all pass (wave/headless/golden unaffected); release-strict 0. Controller stays clock-free; throttle is shell-only.
- [ ] Artifact shows the chrome reflection/highlight tracking the orbit.
