// aleph.edit:controller — STUB (Phase 6, SPEC §3.2).
//
// `EditorController` is the headless, testable core of the structural editor:
// it owns the typed scene graph (the source of truth), lowers it, builds both
// backends (SceneRT + Scene), and turns editor gestures into Ops:
//
//   pick(px,py) -> face -> NodeId
//   apply(Op)   -> apply_op + lower_incremental + rebuild (byte-consistent)
//   camera()    -> OrbitCamera
//
// No SDL/window dependency — pure logic (aleph_editor/aleph_window are NOT
// linked; the controller is headless). `aleph.edit` is the sanctioned
// cross-cutter (like aleph.lowering): it may know all backends.
//
// This is an EMPTY-but-valid scaffold: a minimal class that compiles. The real
// pick/apply/rebuild/camera surface lands in Phase 6 W2.

export module aleph.edit:controller;

import aleph.graph;  // Graph (the truth)

export namespace aleph::edit {

// STUB: minimal headless editor controller. Takes ownership of the initial
// graph. Full surface (selected/select/pick/apply/camera/raster_scene/
// render_scene) arrives in Phase 6 W2.
class EditorController {
public:
    explicit EditorController(aleph::graph::Graph /*initial*/) {}
};

}  // namespace aleph::edit
