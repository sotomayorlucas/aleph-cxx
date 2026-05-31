// Part — `:grouping`: derive light groups from the VisibilitySheaf's H⁰
// connected components (Phase 5.x-a, SPEC §4.1).
//
// STUB (Phase 5.x-a scaffold): `light_groups_of` returns an empty grouping.
// Behavior — building the VisibilitySheaf, running `compute_h0`, and mapping
// Components to groups of Light nodes — is filled by Wave 1.
//
// Architectural boundary (SPEC §1): the sheaf reaches the renderer ONLY through
// the lowering. This partition is allowed to import `aleph.sheaf`; the renderer
// (`aleph.render.rt`) is not — it consumes the plain `light_groups` table.

module;
#include <vector>

export module aleph.lowering:grouping;

import aleph.graph;  // Graph
import aleph.types;  // NodeId

export namespace aleph::lowering {

// Partition the graph's Light nodes into groups by VisibilitySheaf H⁰
// components (SPEC §4.1). STUB: returns empty until Wave 1.
[[nodiscard]] inline std::vector<std::vector<aleph::types::NodeId>>
light_groups_of(const aleph::graph::Graph&) {
    return {};
}

}  // namespace aleph::lowering
