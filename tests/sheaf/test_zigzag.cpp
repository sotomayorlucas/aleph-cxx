// Unit tests for aleph.sheaf:zigzag — zigzag persistence over DPO rewrite
// trajectories.
//
// Oracles ported verbatim from the Rust reference tests:
//   * aleph-engine/aleph-sheaf/tests/zigzag_fixtures.rs   (hand-computed
//     barcodes on n-cycle triangulations)
//   * aleph-engine/aleph-sheaf/tests/zigzag_mv_witness.rs (trajectory MV
//     invariant  Σ ε_sheaf ≥ #deaths_forward)
//   * aleph-engine/aleph-sheaf/tests/zigzag_smoke.rs      (empty-trace edge
//     cases for H0/H1, constant + visibility)
//   * the in-module #[test]s of zigzag.rs                  (new trace shape;
//     empty-barcode finite-interval count)
//
// PORT ADAPTATION (load-bearing). The Rust drives each rewrite via
// aleph_dpo's TRIANGULATE rule + find_one_match + apply_rule, then clones the
// graph into RewriteTrace.graphs. In C++:
//   * Graph is move-only, so RewriteTrace holds non-owning `const Graph*`
//     descriptors; the test owns one Graph per stop and keeps them alive.
//   * There is NO TRIANGULATE rule in aleph.dpo (only spawn_light /
//     remove_object / replace_material / refine_cell), and aleph.dpo::apply
//     gates on aleph::graph::validate_all — which a bare mesh n-cycle fails
//     (CameraExclusive, MaterialReferenced). So, exactly like the
//     :mayer_vietoris test's triangulation case, each rewrite stop is built BY
//     HAND: a triangulating diagonal is the only structural change, deleting
//     nothing. The before/after flag complexes are therefore identical to what
//     TRIANGULATE-on-the-first-2-path-match produces (the first VF2 match on an
//     n-cycle is the path m0—m1—m2, adding the diagonal m0—m2), so the
//     hand-built trajectory reproduces the Rust barcode oracle exactly.
//   * `preserved` is the set of surviving mesh nodes (triangulation deletes
//     nothing): a faithful superset of the rule interface. It feeds only the MV
//     certificate's K; the zigzag arrows use the pushout complement U (= the
//     whole before-graph), so the barcode is independent of this choice.

#include "doctest.h"

#include <cstddef>
#include <string>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.containers;

using aleph::containers::FlatSet;
using aleph::graph::Graph;
using aleph::sheaf::ArrowDirection;
using aleph::sheaf::compute_zigzag_barcode;
using aleph::sheaf::RewriteTrace;
using aleph::sheaf::SheafKind;
using aleph::sheaf::ZigzagBarcode;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// Build an n-cycle of mesh nodes (m_i —Adjacent— m_{(i+1) mod n}) on a fresh
// graph. Node ids are allocated 0..n-1 in order (allocation is deterministic
// per graph), so calling this on any fresh Graph yields the SAME ids — which is
// what lets us build the before/after rewrite stops by hand with matching ids.
// Returns the node ids so callers can add diagonals.
std::vector<NodeId> build_n_cycle(Graph& g, std::size_t n) {
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

void add_diagonal(Graph& g, NodeId a, NodeId b) {
    auto r = g.add_edge(EdgeKind::Adjacent, a, b);
    REQUIRE(r.has_value());
}

// All mesh node ids of `g`, as the preserved set (triangulation deletes none).
FlatSet<NodeId> all_meshes(const Graph& g) {
    FlatSet<NodeId> preserved;
    for (auto [id, node] : g.nodes()) {
        if (aleph::types::kind_of(node) == aleph::types::NodeKind::Mesh) preserved.insert(id);
    }
    return preserved;
}

}  // namespace

// ── In-module zigzag.rs unit oracles ─────────────────────────────────────────

// new_trace_has_one_graph_no_steps
TEST_CASE("new trace has one graph and no steps") {
    const Graph g;
    const RewriteTrace t(g, SheafKind::ConstantZ2);
    CHECK(t.graphs().size() == 1);
    CHECK(t.len() == 0);
    CHECK(t.is_empty());
}

// empty_barcode_has_zero_finite_intervals
TEST_CASE("empty barcode has zero finite intervals") {
    const ZigzagBarcode b{.dim = 1, .last_graph_index = 0, .intervals = {}};
    CHECK(b.finite_intervals() == 0);
}

// ── zigzag_smoke.rs: empty-trace edge cases ──────────────────────────────────

// empty_trace_empty_graph_yields_zero_intervals_h0
TEST_CASE("empty trace empty graph yields zero H0 intervals") {
    const Graph g;
    const RewriteTrace t(g, SheafKind::ConstantZ2);
    const ZigzagBarcode bc = compute_zigzag_barcode(t, 0);
    CHECK(bc.intervals.size() == 0);
    CHECK(bc.last_graph_index == 0);
}

// empty_trace_three_isolated_meshes_yields_three_h0_intervals
TEST_CASE("empty trace three isolated meshes yields three H0 intervals") {
    Graph g;
    for (std::size_t i = 0; i < 3; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
    }
    const RewriteTrace t(g, SheafKind::ConstantZ2);
    const ZigzagBarcode bc = compute_zigzag_barcode(t, 0);
    CHECK(bc.intervals.size() == 3);
    for (const auto& iv : bc.intervals) {
        CHECK(iv.birth == 0);
        CHECK(iv.death == 0);
        REQUIRE(iv.killing_arrow.has_value());
        CHECK(*iv.killing_arrow == ArrowDirection::EndOfTrace);
    }
}

// empty_trace_h1_is_empty_for_acyclic
TEST_CASE("empty trace H1 is empty for acyclic") {
    Graph g;
    for (std::size_t i = 0; i < 3; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
    }
    const RewriteTrace t(g, SheafKind::ConstantZ2);
    const ZigzagBarcode bc = compute_zigzag_barcode(t, 1);
    CHECK(bc.intervals.size() == 0);
}

// visibility_empty_graph_zero_intervals
TEST_CASE("visibility empty graph zero intervals") {
    const Graph g;
    const RewriteTrace t(g, SheafKind::Visibility);
    const ZigzagBarcode bc_h0 = compute_zigzag_barcode(t, 0);
    const ZigzagBarcode bc_h1 = compute_zigzag_barcode(t, 1);
    CHECK(bc_h0.intervals.size() == 0);
    CHECK(bc_h1.intervals.size() == 0);
}

// ── zigzag_fixtures.rs: hand-computed barcodes ───────────────────────────────

// fixture_a_4cycle_triangulate_constant_h1_dies
//
// 4-cycle → one triangulate (diagonal m0—m2) fills BOTH 3-cliques {m0,m1,m2}
// and {m0,m2,m3}; the cycle becomes contractible: H1(G0)=1, H1(G1)=0. A single
// Forward-direction death. One interval [0, 0, Forward].
TEST_CASE("fixture A: 4-cycle triangulate constant H1 dies forward") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 4);

    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 4);  // same ids as g0
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);  // the triangulating diagonal

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));

    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 1);
    REQUIRE(bc.intervals.size() == 1);
    CHECK(bc.intervals[0].birth == 0);
    CHECK(bc.intervals[0].death == 0);
    REQUIRE(bc.intervals[0].killing_arrow.has_value());
    CHECK(*bc.intervals[0].killing_arrow == ArrowDirection::Forward);
}

// fixture_a_6cycle_triangulate_persists_through_zigzag
//
// 6-cycle → triangulate (diagonal m0—m2) fills only ONE 3-clique {m0,m1,m2}.
// The 6-cycle's H1 class includes into G1 as a non-trivial class (homologous to
// the 5-cycle through the new diagonal). The single H1 feature persists across
// the zigzag. One interval [0, 1, EndOfTrace].
TEST_CASE("fixture A: 6-cycle triangulate persists through zigzag") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 6);

    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 6);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));

    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 1);
    REQUIRE(bc.intervals.size() == 1);
    CHECK(bc.intervals[0].birth == 0);
    CHECK(bc.intervals[0].death == 1);
    REQUIRE(bc.intervals[0].killing_arrow.has_value());
    CHECK(*bc.intervals[0].killing_arrow == ArrowDirection::EndOfTrace);
}

// fixture_a_4cycle_triangulate_constant_h0_one_component_throughout
//
// 4-cycle is connected and stays connected. One H0 class throughout.
// One interval [0, 1, EndOfTrace].
TEST_CASE("fixture A: 4-cycle triangulate constant H0 one component throughout") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 4);

    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 4);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));

    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 0);
    REQUIRE(bc.intervals.size() == 1);
    CHECK(bc.intervals[0].birth == 0);
    CHECK(bc.intervals[0].death == 1);
    REQUIRE(bc.intervals[0].killing_arrow.has_value());
    CHECK(*bc.intervals[0].killing_arrow == ArrowDirection::EndOfTrace);
}

// fixture_b_5cycle_two_triangulates_constant_h1_dies_at_step_two
//
// 5-cycle: H1 = 1.
// Step 0 (diagonal m0—m2): fills {m0,m1,m2}. H1 still = 1 (the cycle persists
//   via m0—m2—m3—m4—m0, a 4-cycle through the diagonal).
// Step 1 (diagonal m0—m3): fills {m0,m2,m3} and {m0,m3,m4}; the pentagon is now
//   fully fan-triangulated from m0. H1 drops to 0.
// Expected barcode: one persistent interval [0, 1, Forward] — the original
// 5-cycle class survives step 0 and dies at step 1.
TEST_CASE("fixture B: 5-cycle two triangulates constant H1 dies at step two") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 5);

    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 5);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    Graph g2;
    const std::vector<NodeId> ms2 = build_n_cycle(g2, 5);
    REQUIRE(ms == ms2);
    add_diagonal(g2, ms[0], ms[2]);
    add_diagonal(g2, ms[0], ms[3]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));
    trace.record_step(g1, g2, all_meshes(g1));
    REQUIRE(trace.len() == 2);

    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 1);
    REQUIRE(bc.intervals.size() == 1);
    CHECK(bc.intervals[0].birth == 0);
    CHECK(bc.intervals[0].death == 1);
    REQUIRE(bc.intervals[0].killing_arrow.has_value());
    CHECK(*bc.intervals[0].killing_arrow == ArrowDirection::Forward);
}

// ── zigzag_mv_witness.rs: trajectory MV invariant  Σ ε_sheaf ≥ deaths_forward ─

namespace {

// Assert the trajectory MV inequality for the H1 barcode of `trace`.
void check_mv_trajectory_inequality(const RewriteTrace& trace) {
    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 1);
    std::size_t total_eps = 0;
    for (const auto& step : trace.steps()) total_eps += step.mv.epsilon_sheaf;
    std::size_t deaths_fwd = 0;
    for (const auto& iv : bc.intervals) {
        if (iv.killing_arrow.has_value() && *iv.killing_arrow == ArrowDirection::Forward) {
            ++deaths_fwd;
        }
    }
    CHECK_MESSAGE(total_eps >= deaths_fwd,
                  "trajectory MV inequality: total_eps must dominate forward deaths");
}

}  // namespace

// fixture_a_4cycle_triangulate_mv_invariant
TEST_CASE("MV invariant: 4-cycle triangulate") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 4);
    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 4);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));
    check_mv_trajectory_inequality(trace);
}

// fixture_a_6cycle_triangulate_mv_invariant
TEST_CASE("MV invariant: 6-cycle triangulate") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 6);
    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 6);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));
    check_mv_trajectory_inequality(trace);
}

// fixture_b_5cycle_two_triangulates_mv_invariant
TEST_CASE("MV invariant: 5-cycle two triangulates") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 5);
    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 5);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);
    Graph g2;
    const std::vector<NodeId> ms2 = build_n_cycle(g2, 5);
    REQUIRE(ms == ms2);
    add_diagonal(g2, ms[0], ms[2]);
    add_diagonal(g2, ms[0], ms[3]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));
    trace.record_step(g1, g2, all_meshes(g1));
    check_mv_trajectory_inequality(trace);
}

// ── ε_sheaf is genuinely the death witness on the 4-cycle (constant sheaf) ────
//
// The 4-cycle triangulation kills the constant H1 class with a Forward death,
// and the recorded MV certificate's ε_sheaf is exactly the rank of the
// connecting morphism that witnesses it (connecting_morphism.rs M3b claim).
// Pins the inequality to a tight, non-degenerate value: ε_sheaf ≥ 1 = #deaths.
TEST_CASE("4-cycle triangulate records nonzero epsilon_sheaf witnessing the death") {
    Graph g0;
    const std::vector<NodeId> ms = build_n_cycle(g0, 4);
    Graph g1;
    const std::vector<NodeId> ms1 = build_n_cycle(g1, 4);
    REQUIRE(ms == ms1);
    add_diagonal(g1, ms[0], ms[2]);

    RewriteTrace trace(g0, SheafKind::ConstantZ2);
    trace.record_step(g0, g1, all_meshes(g0));
    REQUIRE(trace.len() == 1);

    const ZigzagBarcode bc = compute_zigzag_barcode(trace, 1);
    std::size_t deaths_fwd = 0;
    for (const auto& iv : bc.intervals) {
        if (iv.killing_arrow.has_value() && *iv.killing_arrow == ArrowDirection::Forward) {
            ++deaths_fwd;
        }
    }
    CHECK(deaths_fwd == 1);
    CHECK(trace.steps()[0].mv.epsilon_sheaf >= 1);
    CHECK(trace.steps()[0].mv.epsilon_sheaf >= deaths_fwd);
}
