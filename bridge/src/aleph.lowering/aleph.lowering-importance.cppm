// Part — `:importance`: per-entity importance from `aleph.flow` Ricci curvature.
// See SPEC §4.1 (Phase 5.x-b — adaptive-spp). STUB: empty-but-valid; later
// waves fill behavior. `aleph.lowering` is the sanctioned cross-cutter that may
// touch `aleph.flow`; `aleph.scene`/`render.rt` never do.

export module aleph.lowering:importance;

import aleph.graph;       // Graph
import aleph.types;       // NodeId
import aleph.flow;        // ollivier_ricci (W1 fills behavior)
import aleph.containers;  // OrderedMap

export namespace aleph::lowering {

// Per-entity importance keyed by Mesh NodeId. STUB: returns empty (⇒ uniform).
// W1 replaces with the deterministic Ricci aggregate over the mesh skeleton.
[[nodiscard]] inline containers::OrderedMap<types::NodeId, double>
entity_importance(const aleph::graph::Graph&) {
    return {};
}

}  // namespace aleph::lowering
