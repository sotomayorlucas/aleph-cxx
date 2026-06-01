# Phase 6 — Structural Editor (`aleph.edit` + `aleph_edit`)

**Status:** design for review.
**Date:** 2026-05-31.
**Predecessor:** Phase 5 v1 + 5.x a/b/c on `main`.

## 1. Goal

An interactive editor where the user edits the **typed scene graph** (the source of truth) — every gesture is an `Op` → `apply_op` → `lower_incremental` → re-render. The editor edits **mathematical objects** (graph nodes), not triangles. This closes the engine loop visibly:

```
gesture ─▶ Op ─▶ apply_op (typed mutation / DPO) ─▶ Graph ─▶ lower_incremental ─▶ {RenderScene, SceneRT} ─▶ pixels
```

**Hybrid view (chosen):** the **software rasterizer** drives a fluid view while navigating/editing; on idle (mouse released, no input) a **progressive low-spp path trace** of the *same lowered scene* converges to an accurate image. Any input cancels back to raster.

## 2. Verifiable core vs interactive shell

- **Testable (gates cover these):** `build_sw_scene` (lowered→SceneRT, with face→entity map), the `EditorController` (pick→NodeId, `apply(Op)`→`lower_incremental`→rebuild consistency), and a **`--headless` scripted mode** (apply a fixed `Op` sequence, render a PPM after each — proves the loop with no window).
- **Manual smoke (run it):** the live SDL window + input + hybrid mode-switch. Not auto-tested; verified by running.

## 3. Architecture

### 3.1 New: `bridge/src/aleph.lowering` gains `build_sw_scene`
`build_sw_scene(const LoweredScene&) -> {render::sw::SceneRT, std::vector<NodeId> face_source}` — emit rasterizer faces per lowered primitive: `QuadLocal`→2 tris, `TriLocal`→1 tri, `SphereLocal`→a low-res tessellated mesh (UV sphere, fixed rings/sectors for determinism); record, per face, the source graph `NodeId`. Lives beside `build_render_scene` (the bridge already owns lowered→render). `aleph_lowering` additionally links `aleph_render_sw`. (render.sw/render.rt still never import graph.)

### 3.2 New module: `bridge/src/aleph.edit` — `EditorController` (headless, testable)
```cpp
export module aleph.edit;          // links aleph_lowering, aleph_scene, aleph_render_sw,
                                   // aleph_render_rt, aleph_render_common, aleph_editor,
                                   // aleph_graph, aleph_types, aleph_math, aleph_threads
class EditorController {
public:
    explicit EditorController(graph::Graph initial);     // takes ownership of the truth
    std::optional<types::NodeId> selected() const;
    void select(std::optional<types::NodeId>);
    std::optional<types::NodeId> pick(int px, int py) const;       // raster pick -> face -> NodeId
    std::expected<void, OpError> apply(const lowering::Op&);        // apply_op + lower_incremental + rebuild
    OrbitCamera& camera();
    const render::sw::SceneRT& raster_scene() const;
    const scene::Scene&        render_scene() const;
    // rebuild() (private): lower_incremental(prev, graph, op, rec) -> build_sw_scene + build_render_scene,
    //   refresh face_source / prim_source maps and the camera-independent state.
};
```
Holds: `graph` (truth), `prev` LoweredScene, the SceneRT + Scene + their `*_source` maps, camera, selection. No SDL/window dependency — pure logic.

### 3.3 New app: `apps/aleph_edit` (SDL shell; built only if `ALEPH_HAVE_SDL2`)
Thin loop over `EditorController` + `aleph.window`:
- orbit camera (drag) [`aleph.editor` orbit]; on motion → raster mode.
- left-click → `controller.pick()` → select an entity (highlight its faces).
- UI panel (`aleph.editor` ui): show selected node kind/id; a material color slider → `SetMaterial` Op; buttons/keys: `a` add object, `l` add light, `x` delete selected, arrow/drag → `SetTransform`.
- **idle** (no input for ~250 ms after release) → start progressive path trace (1→N spp), blit each pass; any event → cancel, back to raster.
- `--headless <script> <outdir>`: run a fixed `Op` sequence, write a PPM (raster + a final path-trace) after each step; print entity/light counts. (Testable artifact.)

## 4. v1 Op/gesture set
`SetMaterial` (recolor selected), `SetTransform` (move selected), `AddObject`, `AddLight`, `DeleteObject`. (`ApplyRule` deferred to the editor — it's available via the API but no v1 gesture.)

## 5. Tests (`tests/edit/`)
1. **build_sw_scene_faces:** a lowered scene with 1 sphere + 1 quad → SceneRT has the expected face counts (sphere tessellation rings×sectors×2 + quad 2); every face's `face_source` is the correct entity NodeId; deterministic.
2. **pick_maps_to_node:** construct a controller, aim a known pixel at a known entity → `pick()` returns that entity's NodeId; a miss → nullopt.
3. **apply_relower_consistency:** `controller.apply(SetMaterial)` then the controller's `render_scene()` equals a fresh `lower(graph)`→`build_render_scene` (byte-identical via `lowering_freeze.hpp` on the LoweredScene); same for AddObject/DeleteObject; invariants hold; no dangling faces.
4. **headless_script:** run a fixed Op sequence (add 2 objects, recolor 1, delete 1) through the controller; assert the final entity/light counts + that each step's lowered state == full re-lower; (smoke) PPMs written ≥10 KB.
5. **purity:** `aleph.edit` is the cross-cutter (may know all backends); `render.sw`/`render.rt`/`scene` still contain no graph/lowering/sheaf/flow refs (iso_* stay green).

## 6. Scope
**IN:** `build_sw_scene` + face→entity; `EditorController` (pick/apply/rebuild/camera) hybrid-ready; the SDL shell with the v1 gesture set + idle progressive path-trace + `--headless`. **OUT:** gizmos/transform handles (drag is delta-based), undo/redo stack, multi-select, `ApplyRule` gestures, asset import.

## 7. Success criteria
- `build_sw_scene` deterministic, correct face→entity; `EditorController.apply` byte-consistent with full re-lower; pick resolves to the right NodeId.
- `--headless` scripted run reproduces expected lowered state + writes images; covered by `ctest`.
- `aleph_edit` builds (with SDL2) and runs the live hybrid editor (manual smoke).
- `release-strict` 0 warnings; full `ctest` green; iso hygiene preserved (`aleph.edit` is the only new cross-cutter).

## 8. Waves
- **W1** — `build_sw_scene` (lowered→SceneRT + sphere tessellation + face_source) in the bridge + test 1.
- **W2** — `aleph.edit` `EditorController` (pick/apply/rebuild/camera, both backends) + tests 2,3,4(controller part).
- **W3** — `apps/aleph_edit` SDL shell (hybrid loop, UI, gestures→Ops) + `--headless` scripted mode + test 4(headless artifact) + manual-smoke instructions. Tag `v0.6.0-editor`.
