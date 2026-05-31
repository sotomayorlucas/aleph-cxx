// Unit tests for aleph.sheaf:connecting — the connecting morphism
// ∂: H⁰(F|_K) → H¹(F_{G'}) of the Mayer-Vietoris long exact sequence.
//
// Oracles ported from the Rust reference:
//   * aleph-engine/aleph-sheaf/src/connecting.rs  (in-module #[test]s):
//       - lift_preserves_singleton_section
//       - connecting_morphism_on_isolated_mesh_with_light_is_zero_dim
//   * aleph-engine/aleph-sheaf/tests/connecting_morphism.rs:
//       - connecting_morphism_rank_is_zero_for_visibility_sheaf_on_4_cycle
//         (incl. its precondition that H¹(G') = 1 so the target of ∂ is a
//         genuinely non-trivial cohomology space).
//
// Not ported here: connecting_morphism.rs's TRIANGULATE-on-4-cycle constant-
// sheaf test (`triangulation_yields_nonzero_epsilon_sheaf_on_constant_sheaf`)
// and the SPAWN_LIGHT MV-closure test. Both drive ∂ through aleph_dpo's
// apply_rule + aleph_sheaf::mayer_vietoris's decompose_rewrite /
// mayer_vietoris_certify, which are not part of the :connecting partition and
// are not yet ported to C++ (:mayer_vietoris is a later wave). The exact
// rank(∂) value in that test is a function of the U/K/R cover that
// decompose_rewrite produces; reconstructing it by hand without the real MV
// machinery would be improvising the geometry, so it is intentionally deferred
// to the :mayer_vietoris test rather than weakened or guessed at here.
//
// What IS pinned at the :connecting surface for the constant sheaf is its
// structural rank-0 behaviour on a single-vertex interface (the constant-sheaf
// analogue of the visibility structural fact): lifting a single H⁰(K) class
// yields a δ⁰_{G'} column, which is in im(δ⁰_{G'}) and therefore trivial in
// H¹(G').

#include "doctest.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.linalg.gf2;

using aleph::graph::Graph;
using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;
using aleph::sheaf::CochainLayout;
using aleph::sheaf::ConstantZ2Sheaf;
using aleph::sheaf::FlagComplex;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::Subgraph;
using aleph::sheaf::VisibilitySheaf;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::coboundary_matrix;
using aleph::sheaf::connecting_morphism;
using aleph::sheaf::lift_k_to_g_prime;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::LightKind;
using aleph::types::Mesh;
using aleph::types::Node;
using aleph::types::NodeId;

namespace {

NodeId add_mesh(Graph& g) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Node{Mesh{id, "m", 1}});
    return id;
}

NodeId add_light(Graph& g) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Node{Light{id, LightKind::Point, "p"}});
    return id;
}

// dim H^k via the rank-nullity formula compute_hk uses:
//   dim H^k = (dim C^k − rank δ^k) − rank δ^{k-1}, with δ^{-1} := 0.
// Computed here from the exported :cochain coboundary_matrix so a test can
// assert the H¹(G') precondition of the 4-cycle oracle without reaching into
// :connecting's internal helpers.
template <typename S>
std::size_t h_dim(const S& sheaf, const FlagComplex& cx, std::size_t k) {
    const CochainLayout layout = CochainLayout::for_dim(sheaf, cx, k);
    const std::size_t cochain_dim = layout.total_bits();
    const std::size_t rank_curr = coboundary_matrix(sheaf, cx, k).rank();
    const std::size_t rank_prev =
        (k == 0) ? std::size_t{0} : coboundary_matrix(sheaf, cx, k - 1).rank();
    const std::size_t kernel_dim = (cochain_dim > rank_curr) ? cochain_dim - rank_curr : 0;
    return (kernel_dim > rank_prev) ? kernel_dim - rank_prev : 0;
}

}  // namespace

// Oracle: connecting.rs::lift_preserves_singleton_section.
// Single mesh with one light, K = G' = the same graph. The lift of a section in
// K equals the section in G' (stalks agree): bit 0 stays set and the popcount
// stays 1.
TEST_CASE("lift preserves a singleton section when K equals G'") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    const CochainLayout layout = CochainLayout::for_dim(sheaf, cx, 0);
    BitVec k_vec(layout.total_bits());
    k_vec.set(0, true);

    const BitVec lifted = lift_k_to_g_prime(k_vec, layout, sheaf, layout, sheaf);
    CHECK(lifted.get(0));
    CHECK(lifted.popcount() == 1);
}

// Oracle: connecting.rs::connecting_morphism_on_isolated_mesh_with_light_is_zero_dim.
// K = G' = single mesh with one light. H⁰(K) = 1, but G' has no edges so
// H¹(G') = 0; ∂ is the zero map and rank(∂) = 0.
TEST_CASE("connecting morphism on an isolated mesh with a light has rank 0") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    const BitMatrix partial = connecting_morphism(sheaf, cx, sheaf, cx);
    CHECK(partial.rank() == 0);
}

// Oracle: connecting_morphism.rs::
//         connecting_morphism_rank_is_zero_for_visibility_sheaf_on_4_cycle.
// 4-cycle of meshes, one shared light influencing all four. The cycle gives
// H¹(G') = 1 (a genuinely non-trivial target), yet rank(∂) = 0: every
// visibility-sheaf section lifts to a column of δ⁰_{G'}, which is in im(δ⁰_{G'})
// by construction and therefore trivial in H¹. K = {ms[0]} (single interface
// vertex — the natural MV choice).
TEST_CASE("connecting morphism is rank 0 for the visibility sheaf on a 4-cycle") {
    Graph g;
    std::array<NodeId, 4> ms{NodeId{}, NodeId{}, NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 4; ++i) ms[i] = add_mesh(g);
    const NodeId l = add_light(g);

    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % 4]).has_value());
        REQUIRE(g.add_edge(EdgeKind::Influences, l, ms[i]).has_value());
    }

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf g_sheaf = VisibilitySheaf::build_full(g, cx);

    // Precondition from the Rust test: H¹(G') = 1, so the target of ∂ is a
    // genuinely non-trivial cohomology space (a 4-cycle with no diagonal has no
    // 2-simplices, so the single shared light yields one 1-cohomology class).
    CHECK(h_dim(g_sheaf, cx, 1) == 1);

    // K = {ms[0]}.
    Subgraph k_sub;
    k_sub.node_ids.insert(ms[0]);
    [[maybe_unused]] const auto [k_skel, k_cx] = k_sub.flag_complex(g);
    const VisibilitySheaf k_sheaf = VisibilitySheaf::build_full_from_subgraph(g, k_sub, k_cx);

    const BitMatrix partial = connecting_morphism(g_sheaf, cx, k_sheaf, k_cx);

    // ∂ is zero: every visibility-sheaf section lifts to a column of δ⁰_{G'},
    // which is in im(δ⁰_{G'}) by definition → trivial in H¹.
    CHECK(partial.rank() == 0);
}

// Structural fact for the CONSTANT Z/2 sheaf at the :connecting surface: a
// single-vertex H⁰(K) class lifts (via the identity lift_basis_index 0 ↦ 0) to
// the G' indicator at that vertex; δ⁰_{G'} of a single-vertex indicator is a
// column of δ⁰_{G'}, which lies in im(δ⁰_{G'}) → trivial in H¹(G'). So with
// K = a single vertex, rank(∂) = 0 even when H¹(G') ≠ 0.
//
// (The non-zero constant-sheaf rank — connecting_morphism.rs's TRIANGULATE
// claim — arises only for the disconnected multi-vertex interface that
// decompose_rewrite builds; that case lives with the deferred :mayer_vietoris
// port, see the file header.)
TEST_CASE("connecting morphism is rank 0 for the constant sheaf on a single-vertex K") {
    // G' = 4-mesh cycle (no diagonal): H¹ of the constant sheaf = 1.
    Graph g;
    std::array<NodeId, 4> ms{NodeId{}, NodeId{}, NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 4; ++i) ms[i] = add_mesh(g);
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % 4]).has_value());
    }

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const ConstantZ2Sheaf g_sheaf(cx);

    // Constant-sheaf H¹ counts independent 1-cycles: a 4-cycle has exactly 1.
    CHECK(h_dim(g_sheaf, cx, 1) == 1);

    // K = {ms[0]} (single interface vertex).
    Subgraph k_sub;
    k_sub.node_ids.insert(ms[0]);
    [[maybe_unused]] const auto [k_skel, k_cx] = k_sub.flag_complex(g);
    const ConstantZ2Sheaf k_sheaf(k_cx);

    const BitMatrix partial = connecting_morphism(g_sheaf, cx, k_sheaf, k_cx);
    CHECK(partial.rank() == 0);
}
