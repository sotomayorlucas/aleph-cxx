// Tier-1 gate for bounded-support curvature + exact MV localization (slice 3).
//
// The HARD gate (now ACHIEVABLE): the localized bounded Laplacian Delta produced
// by build_laplacian_local MUST equal the full build_laplacian_bounded Delta
// BIT-FOR-BIT (==, not approx) on an arbitrary rewrite trace. This holds BY
// CONSTRUCTION: the editor/sim curvature is the bounded-support Ollivier-Ricci
// kappa_R(a, b) computed over the radius-R ball B_R(a, b) (R = kCurvRadius = 2),
// so kappa_R(e) is a PURE FUNCTION of B_R(e). A non-dirty edge's ball is
// unchanged across the edit, so its cached kappa_R == the full rebuild's kappa_R
// bit-for-bit (same local node set, same sorted order, same local `n`, same
// wasserstein_1) -> no global-`n` perturbation drift (the prior blocker under
// the GLOBAL-support W_1). The dirty set is the R-hop ball of the edit.
//
// What this suite proves:
//   Task 1 — bounded curvature:
//     * build_laplacian_bounded is deterministic (byte-identical run-to-run);
//     * it is a valid graph Laplacian (symmetric, ones-in-kernel);
//     * an interior edge's kappa_R(R=2) matches the GLOBAL ricci_curvature_edge
//       within ~1e-6 (R=2 captures the local geometry; only perturbation-`n`
//       differs).
//   Task 2 — exact localization:
//     * local.matrix.at(i,j) == full.matrix.at(i,j) for ALL i,j, BIT-EXACT,
//       every kappa exact, rc == dirty.size() < |E|, on a single add AND a
//       multi-edit trace (adds + a delete).

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

using aleph::flow::build_laplacian_bounded;
using aleph::flow::build_laplacian_local;
using aleph::flow::default_weight;
using aleph::flow::ricci_curvature;
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

// Add one Mesh node adjacent to each node in `to`. Returns the new node id.
NodeId add_object(Graph& g, const std::vector<NodeId>& to) {
    const NodeId c = g.alloc_node_id();
    g.insert_node(Mesh{c, std::string("add") + std::to_string(c.value), 1});
    for (const NodeId t : to) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, c, t).has_value());
    }
    return c;
}

// Assert localized bounded Delta == full bounded Delta BIT-FOR-BIT, plus the
// per-kappa exactness and the win counter. Returns dirty.size().
std::size_t assert_local_eq_full_bounded(const Graph&               g_after,
                                         const WeightedLaplacian&   prev,
                                         const std::vector<NodeId>& seed) {
    const WeightedLaplacian full =
        build_laplacian_bounded(g_after, default_weight);
    const OneSkeleton skel  = OneSkeleton::from_graph(g_after);
    const auto        dirty = two_hop_touched_edges(skel, seed);

    int                     rc = 0;
    const WeightedLaplacian local =
        build_laplacian_local(g_after, prev, dirty, default_weight, &rc);

    // node order is a pure function of the id set.
    REQUIRE(local.node_order == full.node_order);
    REQUIRE(local.curvatures.size() == full.curvatures.size());
    REQUIRE(local.matrix.rows() == full.matrix.rows());
    REQUIRE(local.matrix.cols() == full.matrix.cols());

    // THE GO/NO-GO: every matrix entry BIT-EXACT (==, not approx).
    for (std::size_t i = 0; i < full.matrix.rows(); ++i) {
        for (std::size_t j = 0; j < full.matrix.cols(); ++j) {
            CHECK(local.matrix.at(i, j) == full.matrix.at(i, j));
        }
    }

    // Every curvature BIT-EXACT.
    for (const auto& [e, kf] : full.curvatures) {
        const f64* kl = local.curvatures.get(e);
        REQUIRE(kl != nullptr);
        CHECK(*kl == kf);
    }

    // Win counter targets exactly the dirty set, strictly fewer than |E|.
    CHECK(rc == static_cast<int>(dirty.size()));
    CHECK(rc < static_cast<int>(full.curvatures.size()));
    return dirty.size();
}

}  // namespace

// ── Task 1: bounded curvature ─────────────────────────────────────────────

// build_laplacian_bounded is fully deterministic: same graph -> byte-identical
// matrix and curvatures run-to-run.
TEST_CASE("mv-local: build_laplacian_bounded is deterministic (byte-identical)") {
    Grid grid = make_grid(5);

    const WeightedLaplacian a = build_laplacian_bounded(grid.g, default_weight);
    const WeightedLaplacian b = build_laplacian_bounded(grid.g, default_weight);

    REQUIRE(a.node_order == b.node_order);
    REQUIRE(a.matrix.rows() == b.matrix.rows());
    REQUIRE(a.matrix.cols() == b.matrix.cols());
    for (std::size_t i = 0; i < a.matrix.rows(); ++i)
        for (std::size_t j = 0; j < a.matrix.cols(); ++j)
            CHECK(a.matrix.at(i, j) == b.matrix.at(i, j));

    REQUIRE(a.curvatures.size() == b.curvatures.size());
    for (const auto& [e, ka] : a.curvatures) {
        const f64* kb = b.curvatures.get(e);
        REQUIRE(kb != nullptr);
        CHECK(ka == *kb);
    }
}

// The bounded Delta is still a valid graph Laplacian: symmetric and the all-ones
// vector lies in its kernel (every row sums to 0).
TEST_CASE("mv-local: build_laplacian_bounded is a valid Laplacian") {
    Grid                    grid = make_grid(5);
    const WeightedLaplacian lap =
        build_laplacian_bounded(grid.g, default_weight);

    CHECK(lap.is_symmetric(1e-12));
    CHECK(lap.ones_in_kernel(1e-12));
}

// Locality fidelity: on a large grid, an INTERIOR edge's bounded kappa_R (R=2)
// matches the GLOBAL ricci_curvature within ~1e-6. R=2 captures the local
// geometry exactly (all support geodesics live in B_2); only the perturbation-`n`
// (ball size vs global support) differs, a ~1e-10..1e-6 effect.
TEST_CASE("mv-local: bounded kappa_R ~= global kappa on an interior edge") {
    Grid grid = make_grid(9);

    const WeightedLaplacian bounded =
        build_laplacian_bounded(grid.g, default_weight);
    const auto global = ricci_curvature(grid.g);

    // An interior horizontal edge, far from every boundary (degree-4 both ends).
    const NodeId                    a = grid.ids[4][4];
    const NodeId                    b = grid.ids[4][5];
    const std::pair<NodeId, NodeId> key =
        (a < b) ? std::pair{a, b} : std::pair{b, a};

    const f64* kb = bounded.curvatures.get(key);
    const f64* kg = global.get(key);
    REQUIRE(kb != nullptr);
    REQUIRE(kg != nullptr);
    CHECK(std::abs(*kb - *kg) < 1e-6);
}

// ── Task 2: exact localization (Tier-1 byte-EXACT — the go/no-go) ──────────

// Single add: build_laplacian_local Delta == build_laplacian_bounded Delta
// BIT-FOR-BIT, every kappa exact, rc == dirty.size() < |E|.
TEST_CASE("mv-local: localized bounded Delta == full BIT-EXACT (single add)") {
    Grid grid = make_grid(5);
    const WeightedLaplacian prev =
        build_laplacian_bounded(grid.g, default_weight);

    const NodeId interior = grid.ids[2][2];
    const NodeId c        = add_object(grid.g, {interior});

    const std::size_t ds = assert_local_eq_full_bounded(
        grid.g, prev, std::vector<NodeId>{c, interior});
    CHECK(ds > 0);
}

// Add touching TWO interior nodes (a 2-edge object) — still byte-exact.
TEST_CASE("mv-local: localized bounded Delta == full BIT-EXACT (2-edge add)") {
    Grid grid = make_grid(6);
    const WeightedLaplacian prev =
        build_laplacian_bounded(grid.g, default_weight);

    const NodeId n1 = grid.ids[2][2];
    const NodeId n2 = grid.ids[3][3];
    const NodeId c  = add_object(grid.g, {n1, n2});

    assert_local_eq_full_bounded(grid.g, prev,
                                 std::vector<NodeId>{c, n1, n2});
}

// Multi-edit trace: several adds followed by a delete, threading `prev` through
// every step. Tier-1 byte-exact MUST hold at EVERY step (the cached kappa_R of
// non-dirty edges is bit-identical to a fresh full bounded rebuild because their
// ball is unchanged).
TEST_CASE("mv-local: multi-edit trace stays BIT-EXACT (adds + a delete)") {
    Grid              grid = make_grid(6);
    WeightedLaplacian prev = build_laplacian_bounded(grid.g, default_weight);

    // Step 1: add c1 on an interior node.
    {
        const NodeId to = grid.ids[2][2];
        const NodeId c1 = add_object(grid.g, {to});
        assert_local_eq_full_bounded(grid.g, prev, std::vector<NodeId>{c1, to});
        prev = build_laplacian_bounded(grid.g, default_weight);
    }

    // Step 2: add c2 spanning two interior nodes.
    NodeId c2 = NodeId{};
    {
        const NodeId t1 = grid.ids[3][3];
        const NodeId t2 = grid.ids[3][4];
        c2              = add_object(grid.g, {t1, t2});
        assert_local_eq_full_bounded(grid.g, prev,
                                     std::vector<NodeId>{c2, t1, t2});
        prev = build_laplacian_bounded(grid.g, default_weight);
    }

    // Step 3: add c3 on another interior node.
    {
        const NodeId to = grid.ids[4][2];
        const NodeId c3 = add_object(grid.g, {to});
        assert_local_eq_full_bounded(grid.g, prev, std::vector<NodeId>{c3, to});
        prev = build_laplacian_bounded(grid.g, default_weight);
    }

    // Step 4: DELETE c2 (cascade removes its incident edges). The seed for a
    // delete is the endpoints of the removed edges that survive (its old
    // neighbours) — captured BEFORE the delete.
    {
        const NodeId t1 = grid.ids[3][3];
        const NodeId t2 = grid.ids[3][4];
        grid.g.remove_node_cascade(c2);
        // After cascade, c2 is gone; seed from its surviving old neighbours.
        const std::size_t ds = assert_local_eq_full_bounded(
            grid.g, prev, std::vector<NodeId>{t1, t2});
        CHECK(ds > 0);
        prev = build_laplacian_bounded(grid.g, default_weight);
    }
}
