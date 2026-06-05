// Tier-1 gate for Mayer-Vietoris localization (physics slice 3).
//
// GOAL (as specified): build_laplacian_local(g_after, prev, dirty, …) must
// produce a Delta BIT-FOR-BIT identical to build_laplacian(g_after, …) (the
// full rebuild) on a grid + add-node edits, by reusing cached curvatures for
// non-dirty edges and recomputing only the 2-hop-dirty edges.
//
// FINDING (BLOCKER — recorded honestly, NOT hidden by loosening a tolerance):
// the byte-exact caching gate is UNACHIEVABLE under the current global-support
// Ollivier-Ricci W_1. ricci_curvature_edge computes W_1 over the ENTIRE
// connected component's support (build_state is global; the per-edge support
// slice includes every finite-distance node). Adding a node GROWS that support
// for EVERY edge in the component, so the Charnes-perturbed transportation
// simplex sees a different-dimension problem and its f64 optimum drifts by
// ~1e-10 (and can flip the sign of a near-zero kappa). The cached kappa (from
// g_before's smaller support) is therefore a genuinely different f64 than the
// full rebuild's kappa (g_after's larger support) for edges ARBITRARILY FAR
// from the edit — so no BFS radius < "entire component" makes caching bit-exact
// (and marking the whole component dirty == full rebuild, no win). See the spec
// FOLLOW-UP (bounded-radius BFS support), which is the real fix.
//
// What this suite proves (the parts that ARE sound and load-bearing):
//   1. ricci_curvature_edge factoring is behavior-preserving (existing
//      ollivier_ricci / laplacian tests still pass byte-identically).
//   2. DIRTY edges recomputed in g_after are BIT-EXACT to the full rebuild's
//      kappa (the recompute path + canonical-order assembly are correct).
//   3. The win counter targets exactly the dirty set: rc == dirty.size() < |E|.
//   4. The localized Delta matches the full rebuild to ~1e-10 (approx), and the
//      RESIDUAL byte error is measured and bounded — this is the documented
//      blocker, not a passing gate.

#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.math;

using aleph::flow::build_laplacian;
using aleph::flow::build_laplacian_local;
using aleph::flow::default_weight;
using aleph::flow::two_hop_touched_edges;
using aleph::flow::WeightedLaplacian;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::sheaf::OneSkeleton;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// R x R grid of Mesh nodes joined by 4-neighbour Adjacent edges.
struct Grid {
    Graph                            g;
    std::vector<std::vector<NodeId>> ids;  // ids[r][c]
};

Grid make_grid(std::size_t R) {
    Grid grid;
    grid.ids.assign(R, std::vector<NodeId>(R));
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            const NodeId id = grid.g.alloc_node_id();
            grid.g.insert_node(Mesh{
                id,
                std::string("m") + std::to_string(i) + "_" + std::to_string(j),
                1});
            grid.ids[i][j] = id;
        }
    }
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            if (j + 1 < R) {
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i][j + 1])
                            .has_value());
            }
            if (i + 1 < R) {
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i + 1][j])
                            .has_value());
            }
        }
    }
    return grid;
}

bool edge_in(const std::vector<std::pair<NodeId, NodeId>>& v,
             std::pair<NodeId, NodeId>                      e) {
    for (const auto& x : v)
        if (x == e) return true;
    return false;
}

}  // namespace

// The factoring of detail::ricci_curvature_edge out of the per-edge loop is
// behavior-preserving: the recomputed (in g_after) kappa for every DIRTY edge
// is BIT-EXACT to the full rebuild's kappa, and the node order / structure
// matches. This is the correct, load-bearing half of the slice.
TEST_CASE("mv-local: dirty-edge recompute is BIT-EXACT to full rebuild") {
    Grid                    grid  = make_grid(5);
    const WeightedLaplacian full0 = build_laplacian(grid.g, default_weight);

    // Add one Mesh node C adjacent to an interior node.
    const NodeId interior = grid.ids[2][2];
    const NodeId c        = grid.g.alloc_node_id();
    grid.g.insert_node(Mesh{c, "C", 1});
    REQUIRE(grid.g.add_edge(EdgeKind::Adjacent, c, interior).has_value());

    const WeightedLaplacian full1 = build_laplacian(grid.g, default_weight);

    const OneSkeleton         skel1 = OneSkeleton::from_graph(grid.g);
    const std::vector<NodeId> seed{c, interior};
    const auto                dirty = two_hop_touched_edges(skel1, seed);

    int                     rc = 0;
    const WeightedLaplacian local =
        build_laplacian_local(grid.g, full0, dirty, default_weight, &rc);

    // Structure: node order matches the full build exactly.
    REQUIRE(local.node_order == full1.node_order);
    REQUIRE(local.curvatures.size() == full1.curvatures.size());

    // The win counter targets exactly the dirty set, strictly fewer than |E|.
    CHECK(rc == static_cast<int>(dirty.size()));
    CHECK(rc < static_cast<int>(full1.curvatures.size()));

    // Every DIRTY edge's recomputed kappa is BIT-EXACT (==) to the full
    // rebuild's kappa — proving ricci_curvature_edge reuses identical math.
    for (const auto& e : dirty) {
        const f64* kl = local.curvatures.get(e);
        const f64* kf = full1.curvatures.get(e);
        REQUIRE(kl != nullptr);
        REQUIRE(kf != nullptr);
        CHECK(*kl == *kf);  // bit-exact on the recompute path
    }
}

// The 2-hop dirty set is a sound TOPOLOGICAL cover: every edge whose curvature
// genuinely differs by more than fp-solver noise (1e-9) across the edit is
// marked dirty. (Stale-not-dirty edges differ only at the ~1e-10 fp level — the
// global-support W_1 drift documented in the file header, not a missed
// topological change.)
TEST_CASE("mv-local: 2-hop cover catches every >1e-9 curvature change") {
    Grid                    grid  = make_grid(5);
    const WeightedLaplacian full0 = build_laplacian(grid.g, default_weight);

    const NodeId interior = grid.ids[2][2];
    const NodeId c        = grid.g.alloc_node_id();
    grid.g.insert_node(Mesh{c, "C", 1});
    REQUIRE(grid.g.add_edge(EdgeKind::Adjacent, c, interior).has_value());

    const WeightedLaplacian full1 = build_laplacian(grid.g, default_weight);
    const OneSkeleton         skel1 = OneSkeleton::from_graph(grid.g);
    const std::vector<NodeId> seed{c, interior};
    const auto                dirty = two_hop_touched_edges(skel1, seed);

    for (const auto& [e, k1] : full1.curvatures) {
        const f64* k0 = full0.curvatures.get(e);
        if (k0 == nullptr) {
            // brand-new edge: must be dirty.
            CHECK(edge_in(dirty, e));
            continue;
        }
        // A >1e-9 change is a real topological change -> must be in the cover.
        if (std::abs(*k0 - k1) > 1e-9) {
            CHECK(edge_in(dirty, e));
        }
    }
}

// The localized Delta matches the full rebuild to ~1e-10 (approx). The residual
// BYTE error is measured and bounded here — this records the BLOCKER (see file
// header): under the global-support W_1, cached non-dirty kappa drifts ~1e-10
// vs the full rebuild after a node-add, so the gate is approx-equal but NOT
// bit-equal. We assert the measured bound (a regression guard), and explicitly
// document that strict byte-equality FAILS (it is the go/no-go for the slice).
TEST_CASE("mv-local: localized Delta == full to ~1e-10 (byte-exact BLOCKED)") {
    Grid                    grid  = make_grid(5);
    const WeightedLaplacian full0 = build_laplacian(grid.g, default_weight);

    const NodeId interior = grid.ids[2][2];
    const NodeId c        = grid.g.alloc_node_id();
    grid.g.insert_node(Mesh{c, "C", 1});
    REQUIRE(grid.g.add_edge(EdgeKind::Adjacent, c, interior).has_value());

    const WeightedLaplacian full1 = build_laplacian(grid.g, default_weight);
    const OneSkeleton         skel1 = OneSkeleton::from_graph(grid.g);
    const std::vector<NodeId> seed{c, interior};
    const auto                dirty = two_hop_touched_edges(skel1, seed);

    int                     rc = 0;
    const WeightedLaplacian local =
        build_laplacian_local(grid.g, full0, dirty, default_weight, &rc);

    REQUIRE(local.node_order == full1.node_order);

    // Approx-equality holds at 1e-9 (the existing flow-test tolerance).
    CHECK(local.matrix.approx_eq(full1.matrix, 1e-9));

    // Measure the residual byte error and assert a bound. This is the BLOCKER:
    // if this were 0.0 the slice would be byte-exact; it is ~1e-10 because of
    // the global-support W_1 drift, so strict bit-equality is impossible here.
    f64 max_err = 0.0;
    for (std::size_t i = 0; i < full1.matrix.rows(); ++i)
        for (std::size_t j = 0; j < full1.matrix.cols(); ++j)
            max_err = std::max(
                max_err,
                std::abs(local.matrix.at(i, j) - full1.matrix.at(i, j)));
    CHECK(max_err < 1e-9);   // approx bound (regression guard)
    CHECK(max_err > 0.0);    // documents that it is NOT byte-exact (the blocker)
}
