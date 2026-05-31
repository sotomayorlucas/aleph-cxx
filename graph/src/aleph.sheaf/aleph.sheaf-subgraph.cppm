module;
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

export module aleph.sheaf:subgraph;

import aleph.containers;
import aleph.graph;
import aleph.types;

import :skeleton;
import :flag_complex;

// ---------------------------------------------------------------------------
// `Subgraph`: a value-type view of `(V', E') ⊆ (V, E)` for a host `Graph`.
//
// Used to express the U/K/R pieces of a DPO rewrite to the Mayer–Vietoris
// certifier without re-cloning the entire graph. Ported from
// `aleph-engine/aleph-sheaf/src/subgraph.rs`.
//
// PORT SCOPE (sub-phase 4c, Wave 1): this partition is the *value-type
// surface ONLY* — the membership sets plus the induced `one_skeleton()` /
// `flag_complex()` builders (Rust `flag_complex_view`). The Rust
// `h0_with_dim` is intentionally NOT ported here: it becomes the free
// function `compute_subgraph_h0` in `:cohomology` to avoid a
// `:subgraph → :cohomology` dependency inversion.
//
// Membership-set fields keep their Rust names `node_ids` / `edge_ids`
// (NodeId / EdgeId sets) — every Rust call site and the future
// `:mayer_vietoris` consumer reference `node_ids` / `edge_ids`. They are
// backed by `FlatSet` (NOT `OrderedMap`) so `Subgraph` stays copyable: the
// MV cover pieces (U/K/R) are cloned, and `OrderedMap` is move-only.
// ---------------------------------------------------------------------------

export namespace aleph::sheaf {

struct Subgraph {
    // (V') — runtime ids of the nodes in this view.
    aleph::containers::FlatSet<aleph::types::NodeId> node_ids;
    // (E') — runtime ids of the edges in this view.
    aleph::containers::FlatSet<aleph::types::EdgeId> edge_ids;

    Subgraph() = default;

    // Build the 1-skeleton induced by this subgraph viewed through `host`.
    //
    // Vertices = Mesh nodes whose id is in `node_ids`; edges = Adjacent
    // edges whose runtime id is in `edge_ids` and whose endpoints are both
    // selected Mesh vertices. Adjacent is symmetric, so each edge is stored
    // as the canonical `(min, max)` pair (mirrors `OneSkeleton::from_graph`).
    [[nodiscard]] OneSkeleton one_skeleton(const aleph::graph::Graph& host) const {
        OneSkeleton skel{};

        // Collect selected Mesh vertices, then sort+dedup so the edge loop's
        // contains_vertex (binary_search) is valid (mirrors from_graph).
        for (auto [id, node] : host.nodes()) {
            if (node_ids.contains(id)
                && aleph::types::kind_of(node) == aleph::types::NodeKind::Mesh) {
                skel.vertices.push_back(id);
            }
        }
        std::sort(skel.vertices.begin(), skel.vertices.end());
        skel.vertices.erase(
            std::unique(skel.vertices.begin(), skel.vertices.end()),
            skel.vertices.end());

        // Collect canonical Adjacent edges whose runtime id is selected and
        // whose endpoints are both selected Mesh vertices.
        for (auto [eid, e] : host.edges()) {
            if (edge_ids.contains(eid)
                && e.kind == aleph::types::EdgeKind::Adjacent
                && skel.contains_vertex(e.src)
                && skel.contains_vertex(e.dst)) {
                const auto a = (e.src < e.dst) ? e.src : e.dst;
                const auto b = (e.src < e.dst) ? e.dst : e.src;
                skel.edges.emplace_back(a, b);
            }
        }
        std::sort(skel.edges.begin(), skel.edges.end());
        skel.edges.erase(std::unique(skel.edges.begin(), skel.edges.end()),
                         skel.edges.end());

        return skel;
    }

    // Build the 1-skeleton and the flag complex induced by this subgraph
    // viewed through `host` (Rust `flag_complex_view`).
    //
    // Higher simplices are the cliques of the induced 1-skeleton. Returns
    // `(skeleton, complex)` so callers can reuse the skeleton without
    // recomputing it.
    [[nodiscard]] std::pair<OneSkeleton, FlagComplex>
    flag_complex(const aleph::graph::Graph& host) const {
        OneSkeleton skel = one_skeleton(host);
        FlagComplex complex = build_flag_complex(skel);
        return {std::move(skel), std::move(complex)};
    }
};

}  // namespace aleph::sheaf
