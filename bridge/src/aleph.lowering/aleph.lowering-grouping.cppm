// Part — `:grouping`: derive light groups from the VisibilitySheaf's H⁰
// connected components (Phase 5.x-a, SPEC §4.1).
//
// `light_groups_of(graph)` builds the visibility sheaf over the scene's flag
// complex, computes H⁰ (the connected components of the Mesh/Adjacent
// 1-skeleton with their coherent/conflict light bookkeeping), and projects each
// Component onto a GROUP OF LIGHT NodeIds:
//
//   * the lights co-influencing a connected mesh region (one H⁰ Component) =
//     the union of every vertex stalk in that component = coherent ∪ conflict —
//     land in ONE group;
//   * disjoint mesh regions are distinct Components ⇒ distinct groups;
//   * a Light with NO Influences edge into any mesh never appears in any
//     Component, so it forms its own singleton group.
//
// Determinism (SPEC §4.1): Components come out of `compute_h0` in first-seen
// union-find-root order (insertion order). Within a group the lights are sorted
// ascending by NodeId (the visibility sheaf already sorts its LightSets that
// way, and coherent/conflict are ascending), so the union is ascending too.
// Singleton groups for uninfluencing lights are appended in node insertion
// order. Empty groups (a component that no light touches) are never emitted.
//
// Architectural boundary (SPEC §1): the sheaf reaches the renderer ONLY through
// the lowering. This partition is allowed to import `aleph.sheaf`; the renderer
// (`aleph.render.rt`) is not — it consumes the plain `light_groups` table.

module;
#include <vector>

export module aleph.lowering:grouping;

import aleph.containers;  // FlatSet
import aleph.graph;       // Graph
import aleph.sheaf;       // VisibilitySheaf / build_full / compute_h0 / H0 / Component
import aleph.types;       // NodeId / NodeKind / kind_of

export namespace aleph::lowering {

// Partition the graph's Light nodes into groups by VisibilitySheaf H⁰
// components (SPEC §4.1). Lights co-influencing a connected mesh region share a
// group; disjoint regions are separate groups; a Light with no Influences edge
// is its own singleton. Deterministic (insertion order).
[[nodiscard]] inline std::vector<std::vector<aleph::types::NodeId>>
light_groups_of(const aleph::graph::Graph& g) {
    namespace s = aleph::sheaf;
    using aleph::types::NodeId;

    std::vector<std::vector<NodeId>> groups{};

    // The mathematical pipeline (SPEC §4.1): Mesh/Adjacent 1-skeleton → flag
    // complex → visibility sheaf (F(σ) = ⋂ lights influencing σ's vertices) →
    // H⁰ connected components.
    const s::OneSkeleton skel    = s::OneSkeleton::from_graph(g);
    const s::FlagComplex complex = s::build_flag_complex(skel);
    const s::VisibilitySheaf sheaf = s::VisibilitySheaf::build_full(g, complex);
    const s::H0 h0               = s::compute_h0(skel, sheaf);

    // Track which Light NodeIds have been placed in a (non-singleton) group so
    // the singleton pass can pick up the lights that no connected mesh region
    // sees. A sorted set keeps membership checks deterministic and O(log n).
    aleph::containers::FlatSet<NodeId> grouped{};

    // One group per connected mesh region: the lights touching that region are
    // coherent ∪ conflict. Both are sorted ascending by NodeId and disjoint
    // (conflict = union ∖ coherent), so the merged group is ascending and
    // deduped. Components are emitted in compute_h0's insertion order.
    for (const s::Component& c : h0.components) {
        std::vector<NodeId> group{};
        group.reserve(c.coherent_lights.size() + c.conflict_lights.size());

        // Merge the two ascending, disjoint LightSets into one ascending group.
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < c.coherent_lights.size() && j < c.conflict_lights.size()) {
            const NodeId a = c.coherent_lights[i];
            const NodeId b = c.conflict_lights[j];
            if (a < b) { group.push_back(a); ++i; }
            else       { group.push_back(b); ++j; }  // disjoint ⇒ never equal
        }
        for (; i < c.coherent_lights.size(); ++i) group.push_back(c.coherent_lights[i]);
        for (; j < c.conflict_lights.size(); ++j) group.push_back(c.conflict_lights[j]);

        if (group.empty()) continue;  // a region no light influences: no group
        for (const NodeId l : group) grouped.insert(l);
        groups.push_back(std::move(group));
    }

    // Singleton groups: every Light node not already grouped (i.e. with no
    // Influences edge into any connected mesh region) becomes its own group.
    // Walked in node insertion order for determinism.
    for (auto [id, node] : g.nodes()) {
        if (aleph::types::kind_of(node) != aleph::types::NodeKind::Light) continue;
        if (grouped.contains(id)) continue;
        groups.push_back(std::vector<NodeId>{id});
        grouped.insert(id);
    }

    return groups;
}

}  // namespace aleph::lowering
