// Unit tests for aleph.flow:ollivier_ricci.
//
// Oracles ported verbatim from
//   aleph-engine/aleph-flow/src/ollivier_ricci.rs  (the #[cfg(test)] module).
//
// Tolerances are copied EXACTLY from the Rust tests, NOT weakened:
//   - 4-cycle  : |kappa| < 1e-6 on every edge (expected kappa = 0)
//   - 2 meshes : |kappa| < 1e-9                (expected kappa = 0)
//   - 3-cycle  : |kappa - 0.5| < 1e-6          (K_3 triangle)
//   - isolated : empty map
//
// Graphs are built via the real Graph -> OneSkeleton pipeline (Mesh + Adjacent
// edges), the same public surface the Rust tests use through ricci_curvature.

#include "doctest.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.math;

using aleph::flow::ricci_curvature;
using aleph::flow::RicciMap;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// n-cycle of Mesh vertices joined by Adjacent edges (port of the Rust
// `n_cycle` helper). Edges (i, (i+1) % n).
Graph n_cycle(std::size_t n) {
    Graph               g;
    std::vector<NodeId> ms;
    ms.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
        ms.push_back(id);
    }
    for (std::size_t i = 0; i < n; ++i) {
        auto r = g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % n]);
        REQUIRE(r.has_value());
    }
    return g;
}

}  // namespace

// 4-cycle: each vertex has 2 neighbours; W_1(mu_u, mu_v) = 1 for adjacent
// u, v, so kappa = 1 - 1/1 = 0 on every edge.
TEST_CASE("ricci_4cycle_is_zero_on_every_edge") {
    const Graph    g     = n_cycle(4);
    const RicciMap kappa = ricci_curvature(g);
    CHECK(kappa.size() == 4);
    for (const auto& [edge, k] : kappa) {
        (void)edge;
        CHECK(std::abs(k) < 1e-6);
    }
}

// Three isolated meshes, no edges -> empty curvature map.
TEST_CASE("ricci_isolated_meshes_empty_map") {
    Graph g;
    for (std::size_t i = 0; i < 3; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
    }
    const RicciMap kappa = ricci_curvature(g);
    CHECK(kappa.size() == 0);
}

// Two adjacent meshes: mu_0 = (0, 1), mu_1 = (1, 0). W_1 = d(0, 1) = 1,
// so kappa = 1 - 1 = 0.
TEST_CASE("ricci_two_adjacent_meshes_zero") {
    Graph        g;
    const NodeId m0 = g.alloc_node_id();
    const NodeId m1 = g.alloc_node_id();
    g.insert_node(Mesh{m0, "a", 1});
    g.insert_node(Mesh{m1, "b", 1});
    REQUIRE(g.add_edge(EdgeKind::Adjacent, m0, m1).has_value());
    const RicciMap kappa = ricci_curvature(g);
    REQUIRE(kappa.size() == 1);
    const auto& [edge, k] = *kappa.begin();
    (void)edge;
    CHECK(std::abs(k) < 1e-9);
}

// 3-cycle = K_3: for adjacent u, v the common neighbour w carries equal mass
// on both sides, so 1/2 mass shifts from v->u at cost 1 * (1/2). W_1 = 1/2,
// kappa = 1 - 1/2 = 1/2 on every edge.
TEST_CASE("ricci_3cycle_triangle") {
    const Graph    g     = n_cycle(3);
    const RicciMap kappa = ricci_curvature(g);
    CHECK(kappa.size() == 3);
    for (const auto& [edge, k] : kappa) {
        (void)edge;
        CHECK(std::abs(k - 0.5) < 1e-6);
    }
}
