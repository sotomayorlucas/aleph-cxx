// Unit tests for aleph.flow:flow — IncrementalLaplacian + jacobi_eigh.
//
// Oracles ported verbatim from
//   * aleph-engine/aleph-flow/src/flow.rs            (#[cfg(test)] module:
//       from_graph_4cycle_psd, triangulate_incremental_matches_fresh,
//       jacobi_eigh_2x2_matches_known)
//   * aleph-engine/aleph-flow/tests/flow_smoke.rs    (empty_graph_yields_empty_laplacian,
//       isolated_meshes_yield_zero_matrix)
//   * aleph-engine/aleph-flow/tests/incremental_property.rs (the 64-case
//       proptest: incremental factor reconstructs to within 1e-4 of fresh)
//
// Tolerances are copied EXACTLY from the Rust tests, NOT weakened:
//   - from_graph 4-cycle PSD round-trip : 1e-9   (reconstruct ~ laplacian)
//   - isolated meshes zero matrix       : 1e-12  (every entry)
//   - triangulate incremental vs fresh  : 1e-6   (Frobenius)
//   - jacobi_eigh 2x2 M v = lambda v    : 1e-9
//   - proptest incremental vs fresh     : 1e-4   (Frobenius, compounded)
//
// PORT ADAPTATION (load-bearing). The Rust drives the rewrite via aleph_dpo's
// TRIANGULATE rule + find_one_match + apply_rule. There is NO TRIANGULATE rule
// in aleph.dpo, and apply() gates on validate_all (a bare mesh n-cycle fails
// CameraExclusive / MaterialReferenced). So — exactly like the sheaf :zigzag and
// :mayer_vietoris tests — each rewrite stop is built BY HAND: triangulating a
// 5-cycle means adding the diagonal m0—m2 (deleting nothing). The post-state
// node set is identical to the pre-state, so apply_rewrite takes the dense
// rank-k path, reproducing the Rust oracle's incremental-vs-fresh comparison.
// apply_rewrite ignores g_before and preserved (the Rust body does too), so the
// preserved set is the surviving mesh nodes (a faithful superset of the
// interface) and g_before is the pre-state graph.

#include "doctest.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.sparse;
import aleph.math;

using aleph::containers::FlatSet;
using aleph::flow::IncrementalLaplacian;
using aleph::graph::Graph;
using aleph::linalg::sparse::DMatrix;
using aleph::linalg::sparse::LDLT;
using aleph::math::f64;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// n-cycle of Mesh vertices joined by Adjacent edges (port of the Rust `n_cycle`
// helper). Edges (i, (i+1) % n). Returns the node ids in allocation order.
std::vector<NodeId> n_cycle(Graph& g, std::size_t n) {
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
    return ms;
}

// The Dense LDLT factor of an IncrementalLaplacian (the test graphs all stay
// well under SPARSE_THRESHOLD = 32, so the factor is always Dense). REQUIREs
// the Dense alternative, mirroring the Rust `let LaplacianFactor::Dense(..)`.
const LDLT& dense_factor(const IncrementalLaplacian& inc) {
    REQUIRE(inc.factor.index() == 0);
    return std::get<0>(inc.factor);
}

}  // namespace

// flow_smoke.rs::empty_graph_yields_empty_laplacian
TEST_CASE("empty_graph_yields_empty_laplacian") {
    const Graph g;
    auto        r = IncrementalLaplacian::from_graph(g);
    REQUIRE(r.has_value());
    CHECK(r->node_order.size() == 0);
    CHECK(r->laplacian.rows() == 0);
}

// flow_smoke.rs::isolated_meshes_yield_zero_matrix
TEST_CASE("isolated_meshes_yield_zero_matrix") {
    Graph g;
    for (std::size_t i = 0; i < 3; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
    }
    auto r = IncrementalLaplacian::from_graph(g);
    REQUIRE(r.has_value());
    CHECK(r->node_order.size() == 3);
    // No edges -> Laplacian is the zero matrix.
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            CHECK(std::abs(r->laplacian.at(row, col)) < 1e-12);
        }
    }
}

// flow.rs::from_graph_4cycle_psd — n = 4 < SPARSE_THRESHOLD, so Dense; the
// reconstructed factor approximates the Laplacian to 1e-9.
TEST_CASE("from_graph_4cycle_psd") {
    Graph g;
    n_cycle(g, 4);
    auto inc = IncrementalLaplacian::from_graph(g);
    REQUIRE(inc.has_value());
    const LDLT&   f     = dense_factor(*inc);
    const DMatrix recon = f.reconstruct();
    CHECK(recon.approx_eq(inc->laplacian, 1e-9));
}

// flow.rs::triangulate_incremental_matches_fresh — triangulate a 5-cycle (add
// the diagonal m0—m2, deleting nothing). The node set is stable, so
// apply_rewrite takes the dense rank-k path; the incrementally-updated factor
// reconstructs to within 1e-6 (Frobenius) of the fresh-from-scratch factor.
TEST_CASE("triangulate_incremental_matches_fresh") {
    Graph                     g_before;
    const std::vector<NodeId> ms = n_cycle(g_before, 5);

    // g_after = g_before + the triangulating diagonal m0—m2.
    Graph                     g_after;
    const std::vector<NodeId> ms2 = n_cycle(g_after, 5);
    {
        auto r = g_after.add_edge(EdgeKind::Adjacent, ms2[0], ms2[2]);
        REQUIRE(r.has_value());
    }

    // preserved = the surviving mesh nodes (triangulation deletes none).
    FlatSet<NodeId> preserved;
    for (const NodeId id : ms) {
        preserved.insert(id);
    }

    auto inc = IncrementalLaplacian::from_graph(g_before);
    REQUIRE(inc.has_value());
    auto ar = inc->apply_rewrite(g_before, g_after, preserved);
    REQUIRE(ar.has_value());

    auto fresh = IncrementalLaplacian::from_graph(g_after);
    REQUIRE(fresh.has_value());

    const LDLT& fi = dense_factor(*inc);
    const LDLT& ff = dense_factor(*fresh);
    const f64   diff = fi.reconstruct().frobenius_norm_diff(ff.reconstruct());
    CHECK(diff < 1e-6);
}

// flow.rs::jacobi_eigh_2x2_matches_known — for M = [[4,1],[1,3]], each
// (lambda_i, v_i) returned by jacobi_eigh satisfies M v_i = lambda_i v_i to
// 1e-9. jacobi_eigh is the module's internal eigen-routine (the rank-k update's
// eigenpair source); it is exported by the flow partition specifically so this
// in-module Rust oracle ports verbatim, including the first-found p<q
// tie-breaking that fixes the eigenvectors bit-for-bit.
TEST_CASE("jacobi_eigh_2x2_matches_known") {
    const DMatrix m = DMatrix::from_rows({{4.0, 1.0}, {1.0, 3.0}});
    auto [vals, vecs] = aleph::flow::jacobi_eigh(m);
    // Verify each (lambda, v) satisfies M v = lambda v.
    for (std::size_t i = 0; i < 2; ++i) {
        std::vector<f64> v(2, 0.0);
        for (std::size_t r = 0; r < 2; ++r) {
            v[r] = vecs.at(r, i);
        }
        const std::vector<f64> mv = m.matvec(std::span<const f64>(v));
        for (std::size_t k = 0; k < 2; ++k) {
            CHECK(std::abs(mv[k] - v[k] * vals[i]) < 1e-9);
        }
    }
}

// incremental_property.rs — the 64-case proptest, ported as a deterministic
// rewrite trace (no proptest harness in C++): a 4-cycle of meshes is
// repeatedly triangulated by hand, and after each stable-node-set rewrite the
// incrementally-updated factor reconstructs to within 1e-4 (Frobenius) of the
// fresh-from-scratch factor. The 1e-4 tolerance is the compounded-over-rewrites
// bound copied verbatim from the Rust proptest.
TEST_CASE("incremental_factor_matches_fresh_after_n_rewrites") {
    // Start: a 4-cycle of meshes (node set stays fixed across the trace).
    Graph                     g;
    const std::vector<NodeId> ms = n_cycle(g, 4);

    auto inc = IncrementalLaplacian::from_graph(g);
    REQUIRE(inc.has_value());

    FlatSet<NodeId> preserved;
    for (const NodeId id : ms) {
        preserved.insert(id);
    }

    // The only two diagonals of a 4-cycle are (0,2) and (1,3). A deterministic
    // trace cumulatively toggles them on, each rewrite keeping the node set
    // fixed so every step drives the dense rank-k incremental path. After each
    // rewrite the incremental factor must still track the fresh factor (the
    // proptest's intent: incremental == fresh after an arbitrary rewrite trace).
    const std::vector<std::pair<std::size_t, std::size_t>> diagonals = {
        {0, 2}, {1, 3}};

    Graph g_curr;
    n_cycle(g_curr, 4);  // ids match `ms` (fresh graph allocates 0..3)
    std::vector<std::pair<std::size_t, std::size_t>> applied;

    for (const auto& diag : diagonals) {
        applied.push_back(diag);
        // Rebuild the post-state graph from scratch with all diagonals so far,
        // so the post node set is identical (stable) to the pre-state.
        Graph                     g_next;
        const std::vector<NodeId> nxt = n_cycle(g_next, 4);
        for (const auto& [a, b] : applied) {
            auto r = g_next.add_edge(EdgeKind::Adjacent, nxt[a], nxt[b]);
            REQUIRE(r.has_value());
        }
        auto ar = inc->apply_rewrite(g_curr, g_next, preserved);
        REQUIRE(ar.has_value());

        // Per-step certificate: incremental tracks fresh within the compounded
        // bound after every rewrite, not just at the end.
        auto fresh_step = IncrementalLaplacian::from_graph(g_next);
        REQUIRE(fresh_step.has_value());
        const f64 step_diff = dense_factor(*inc).reconstruct().frobenius_norm_diff(
            dense_factor(*fresh_step).reconstruct());
        CHECK(step_diff < 1e-4);

        g_curr = std::move(g_next);
    }

    auto fresh = IncrementalLaplacian::from_graph(g_curr);
    REQUIRE(fresh.has_value());

    const LDLT& fi   = dense_factor(*inc);
    const LDLT& ff   = dense_factor(*fresh);
    const f64   diff = fi.reconstruct().frobenius_norm_diff(ff.reconstruct());
    CHECK(diff < 1e-4);
}
