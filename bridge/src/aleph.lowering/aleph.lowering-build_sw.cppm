// aleph.lowering:build_sw — STUB (Phase 6, SPEC §3.1).
//
// `build_sw_scene(const LoweredScene&) -> SwBuild` emits rasterizer faces per
// lowered primitive (QuadLocal->2 tris, TriLocal->1 tri, SphereLocal->a low-res
// deterministic UV-sphere mesh) and records, per face, the source graph NodeId.
// Lives beside `build_render_scene` (the bridge owns the lowered->render arrow).
// `aleph_lowering` additionally links `aleph_render_sw`; render.sw/render.rt
// still never import graph.
//
// This is an EMPTY-but-valid scaffold: it compiles and returns a default
// (empty) SwBuild. The real lowered->SceneRT translation + sphere tessellation
// + face_source map land in Phase 6 W1.

module;
#include <vector>

export module aleph.lowering:build_sw;

import aleph.render.sw;  // SceneRT
import aleph.types;      // NodeId
import :lowered;         // LoweredScene

export namespace aleph::lowering {

// Result of rasterizing a LoweredScene into the software backend's SceneRT:
// the scene plus a parallel `face_source` map giving, per emitted face, the
// originating graph NodeId (the pick target).
struct SwBuild {
    aleph::render::sw::SceneRT     scene;
    std::vector<aleph::types::NodeId> face_source;
};

// STUB: returns an empty SwBuild. Real implementation in Phase 6 W1.
[[nodiscard]] inline SwBuild build_sw_scene(const LoweredScene& /*lowered*/) {
    return {};
}

}  // namespace aleph::lowering
