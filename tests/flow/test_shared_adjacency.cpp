// Shared skeleton adjacency for bounded kappa_R (spec 2026-07-03).
//
// The per-edge bounded primitive used to rebuild the GLOBAL index+adjacency on
// every call (O(V+E) per edge). These tests pin the refactor's contract: the
// shared-adjacency overload is BITWISE-identical to (1) what the bounded
// builder stores and (2) the fresh-adjacency wrapper, on every skeleton edge.

#include "doctest.h"

#include <string>
#include <utility>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.math;

using aleph::flow::build_adjacency;
using aleph::flow::build_laplacian_bounded;
using aleph::flow::default_weight;
using aleph::flow::ricci_curvature_edge_bounded;
using aleph::flow::SkeletonAdjacency;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::sheaf::OneSkeleton;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// 6x6 grid of Mesh nodes + one attached object (an off-lattice vertex), the
// same shape the Tier-1 mv_localization suite exercises.
Graph make_test_graph() {
    Graph g;
    std::vector<std::vector<NodeId>> ids(6, std::vector<NodeId>(6));
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t j = 0; j < 6; ++j) {
            const NodeId id = g.alloc_node_id();
            g.insert_node(Mesh{
                id,
                std::string("m") + std::to_string(i) + "_" + std::to_string(j),
                1});
            ids[i][j] = id;
        }
    }
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t j = 0; j < 6; ++j) {
            if (j + 1 < 6)
                REQUIRE(g.add_edge(EdgeKind::Adjacent, ids[i][j], ids[i][j + 1])
                            .has_value());
            if (i + 1 < 6)
                REQUIRE(g.add_edge(EdgeKind::Adjacent, ids[i][j], ids[i + 1][j])
                            .has_value());
        }
    }
    const NodeId c = g.alloc_node_id();
    g.insert_node(Mesh{c, "attached", 1});
    REQUIRE(g.add_edge(EdgeKind::Adjacent, c, ids[2][2]).has_value());
    return g;
}

}  // namespace

// The shared-adjacency per-edge primitive reproduces EXACTLY what the bounded
// builder stores: kappa_R(shared) == build_laplacian_bounded(g).curvatures[e]
// bitwise for every skeleton edge (the builder and the primitive are the same
// computation).
TEST_CASE("shared-adj: primitive == builder curvatures, bitwise, every edge") {
    const Graph g = make_test_graph();
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const SkeletonAdjacency shared = build_adjacency(skel);

    const auto lap = build_laplacian_bounded(g, default_weight);

    REQUIRE(lap.curvatures.size() == skel.edges.size());
    for (const auto& [a, b] : skel.edges) {
        const f64  k_shared = ricci_curvature_edge_bounded(skel, shared, a, b);
        const f64* k_built  = lap.curvatures.get(std::pair<NodeId, NodeId>{a, b});
        REQUIRE(k_built != nullptr);
        CHECK(k_shared == *k_built);
    }
}

// The fresh-adjacency wrapper (which builds its own SkeletonAdjacency per
// call) is bitwise-equal to the shared-adjacency overload on every edge: the
// hoisted structures are built by the same loops in the same order.
TEST_CASE("shared-adj: wrapper == shared overload, bitwise, every edge") {
    const Graph g = make_test_graph();
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const SkeletonAdjacency shared = build_adjacency(skel);

    for (const auto& [a, b] : skel.edges) {
        const f64 k_fresh  = ricci_curvature_edge_bounded(skel, a, b);
        const f64 k_shared = ricci_curvature_edge_bounded(skel, shared, a, b);
        CHECK(k_fresh == k_shared);
    }
}
