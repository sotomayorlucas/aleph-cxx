// Part — `:importance`: per-entity importance from `aleph.flow` Ollivier-Ricci
// curvature over the scene's mesh-adjacency skeleton (SPEC §4.1, Phase 5.x-b).
//
//   entity_importance(g) =
//     1. flow::ricci_curvature(g)   — Ollivier-Ricci (W_1) over the `Adjacent`
//        mesh 1-skeleton: one kappa per Adjacent edge (sorted node-id pair).
//     2. per Mesh, MEAN of its incident-edge kappa (a Mesh with no Adjacent
//        edge contributes nothing → raw value 0, "uniform").
//     3. NORMALIZE across meshes to [0,1] by min-max; if all means are equal
//        (or there are no edges at all) every importance is 0 → uniform.
//
// ARCHITECTURAL BOUNDARY (SPEC §1): `aleph.lowering` is the SANCTIONED cross-
// cutter — the only module that may touch `aleph.flow`. `aleph.scene` and
// `aleph.render.rt` NEVER import `aleph.flow`; the importance reaches the
// renderer ONLY as a plain `f32` array baked onto the `Scene` by the lowering.
//
// DETERMINISM (SPEC §7): the result `OrderedMap` is keyed in graph node
// INSERTION order (the meshes are walked via `g.nodes()`), the per-edge kappa
// comes from `flow::ricci_curvature` (all f64, fixed order, no parallelism),
// and the min-max normalization is a pure function of those f64 values. Same
// graph ⇒ byte-identical importance map.
//
// No exceptions (aleph_flags_isa): this is a total function — every code path
// returns a valid map; there is no fallible step (a missing edge / empty graph
// degrades gracefully to all-zero "uniform" importance).

module;
#include <cstddef>
#include <vector>

export module aleph.lowering:importance;

import aleph.graph;       // Graph: nodes()/edges()
import aleph.types;       // NodeId, NodeKind, kind_of, EdgeKind
import aleph.flow;        // ricci_curvature -> OrderedMap<pair<NodeId,NodeId>, f64>
import aleph.containers;  // OrderedMap (insertion-ordered, move-only)

export namespace aleph::lowering {

// Per-entity importance keyed by Mesh `NodeId`, in graph node insertion order.
//
// Runs Ollivier-Ricci (`aleph.flow::ricci_curvature`) over the graph's Adjacent
// mesh skeleton, aggregates the incident-edge curvatures per Mesh by MEAN, then
// min-max normalizes across meshes to [0,1]. All-equal means / no Adjacent edges
// ⇒ every importance is 0 (uniform). Deterministic.
[[nodiscard]] inline containers::OrderedMap<types::NodeId, double>
entity_importance(const aleph::graph::Graph& g) {
    using aleph::types::NodeId;
    using aleph::types::NodeKind;
    namespace t = aleph::types;

    containers::OrderedMap<types::NodeId, double> out{};

    // ── Enumerate Mesh vertices in graph node INSERTION order. This is the key
    // order of the result map (deterministic) and the order we accumulate over.
    std::vector<NodeId> meshes{};
    for (auto [id, node] : g.nodes()) {
        if (t::kind_of(node) == NodeKind::Mesh) {
            meshes.push_back(id);
        }
    }
    if (meshes.empty()) {
        return out;  // no meshes ⇒ empty map (uniform downstream)
    }

    // ── Ollivier-Ricci over the Adjacent mesh skeleton: one kappa per Adjacent
    // edge (canonical sorted node-id pair). Endpoints are Mesh ids by
    // construction (OneSkeleton only admits Mesh–Mesh Adjacent edges).
    const aleph::flow::RicciMap kappa = aleph::flow::ricci_curvature(g);

    // ── Per Mesh: MEAN of incident-edge kappa. A Mesh with no incident Adjacent
    // edge has sum=0, count=0 ⇒ raw mean 0 (it contributes nothing — uniform).
    // Accumulate in `meshes` order so the parallel `raw` array is deterministic.
    const std::size_t n = meshes.size();
    std::vector<double> sum(n, 0.0);
    std::vector<std::size_t> cnt(n, 0);

    // index-of helper over the (small) mesh list, by NodeId.
    auto index_of = [&](NodeId id, std::size_t& out_idx) noexcept -> bool {
        for (std::size_t i = 0; i < n; ++i) {
            if (meshes[i] == id) { out_idx = i; return true; }
        }
        return false;
    };

    bool any_edge = false;
    for (const auto& [edge, k] : kappa) {
        std::size_t ia = 0;
        std::size_t ib = 0;
        const bool ha = index_of(edge.first, ia);
        const bool hb = index_of(edge.second, ib);
        if (ha) { sum[ia] += k; ++cnt[ia]; any_edge = true; }
        if (hb) { sum[ib] += k; ++cnt[ib]; any_edge = true; }
    }

    std::vector<double> raw(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        raw[i] = (cnt[i] > 0) ? (sum[i] / static_cast<double>(cnt[i])) : 0.0;
    }

    // ── Normalize across meshes to [0,1] via min-max. No Adjacent edges at all,
    // or all means equal (zero spread), ⇒ every importance is 0 (uniform). This
    // is the SPEC §4.1 degenerate rule, applied BEFORE division to avoid 0/0.
    double lo = raw[0];
    double hi = raw[0];
    for (std::size_t i = 1; i < n; ++i) {
        if (raw[i] < lo) lo = raw[i];
        if (raw[i] > hi) hi = raw[i];
    }
    const double span = hi - lo;
    const bool uniform = !any_edge || !(span > 0.0);  // (>) is false for NaN too

    for (std::size_t i = 0; i < n; ++i) {
        const double v = uniform ? 0.0 : (raw[i] - lo) / span;
        (void)out.insert(meshes[i], v);
    }
    return out;
}

}  // namespace aleph::lowering
