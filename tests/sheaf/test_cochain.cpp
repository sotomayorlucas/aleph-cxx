#include "doctest.h"

#include <array>
#include <cstddef>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.linalg.gf2;

using aleph::graph::Graph;
using aleph::linalg::gf2::BitMatrix;
using aleph::sheaf::Cochain;
using aleph::sheaf::CochainLayout;
using aleph::sheaf::FlagComplex;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::Simplex;
using aleph::sheaf::VisibilitySheaf;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::coboundary_matrix;
using aleph::sheaf::make_simplex;
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

// cochain.rs::tests::small_fixture — two adjacent meshes m0,m1 and one light l
// influencing both. F({m0}) = F({m1}) = {l} (dim 1 each); the m0–m1 edge stalk
// is also {l}.
struct SmallFixture {
    Graph     g;
    OneSkeleton skel;
    FlagComplex cx;
    VisibilitySheaf sheaf;
};

SmallFixture make_small_fixture() {
    Graph g;
    const NodeId m0 = add_mesh(g);
    const NodeId m1 = add_mesh(g);
    const NodeId l  = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Adjacent, m0, m1).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m0).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m1).has_value());

    OneSkeleton skel = OneSkeleton::from_graph(g);
    FlagComplex cx = build_flag_complex(skel);
    VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);
    return SmallFixture{std::move(g), std::move(skel), std::move(cx), std::move(sheaf)};
}

// Verify δ^{k+1} ∘ δ^k = 0 over every dimension of the complex built from `g`.
// Mirrors cochain.rs (tests/cochain.rs)::check_delta_squared_is_zero.
void check_delta_squared_is_zero(const Graph& g) {
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);
    for (std::size_t k = 0; k < cx.max_dim; ++k) {
        const BitMatrix d_k  = coboundary_matrix(sheaf, cx, k);
        const BitMatrix d_k1 = coboundary_matrix(sheaf, cx, k + 1);
        const BitMatrix composed = d_k1.mul(d_k);
        CHECK(composed.is_zero());  // δ^{k+1} ∘ δ^k must vanish
    }
}

}  // namespace

// Oracle: cochain.rs::tests::layout_for_dim_zero_packs_vertex_stalks
// Two mesh vertices, each with a 1-dim stalk → C^0 packs into 2 bits, 2 ranges.
TEST_CASE("layout for dim zero packs vertex stalks") {
    const SmallFixture fx = make_small_fixture();
    const CochainLayout layout = CochainLayout::for_dim(fx.sheaf, fx.cx, 0);
    CHECK(layout.k() == 0);
    CHECK(layout.total_bits() == 2);
    CHECK(layout.simplex_count() == 2);
}

// Each vertex stalk reserves a half-open range of its own width; the ranges are
// contiguous and start at 0 in level order (the IndexMap insertion order).
TEST_CASE("layout ranges are contiguous and width-correct") {
    const SmallFixture fx = make_small_fixture();
    const CochainLayout layout = CochainLayout::for_dim(fx.sheaf, fx.cx, 0);

    std::size_t expected_start = 0;
    std::size_t seen = 0;
    for (const auto& [sigma, range] : layout.per_simplex()) {
        CHECK(range.start == expected_start);
        CHECK(range.width() == fx.sheaf.dim_stalk(sigma));
        expected_start = range.end;
        ++seen;
    }
    CHECK(seen == 2);
    CHECK(expected_start == layout.total_bits());
}

// A layout for a dimension beyond the complex has zero bits and no simplices.
TEST_CASE("layout for an out-of-range dimension is empty") {
    const SmallFixture fx = make_small_fixture();
    // small_fixture has max_dim 1 (an edge), so level 5 does not exist.
    const CochainLayout layout = CochainLayout::for_dim(fx.sheaf, fx.cx, 5);
    CHECK(layout.k() == 5);
    CHECK(layout.total_bits() == 0);
    CHECK(layout.simplex_count() == 0);
}

// Oracle: cochain.rs::tests::cochain_get_set_round_trip
// A zeroed cochain reads false; setting bit 0 of the first vertex stalk then
// reads true.
TEST_CASE("cochain get/set round trip") {
    const SmallFixture fx = make_small_fixture();
    const CochainLayout layout = CochainLayout::for_dim(fx.sheaf, fx.cx, 0);
    Cochain s = Cochain::zeros(layout);

    // The first 0-simplex in level order (matches Rust's
    // `cx.simplices[0].iter().next()`).
    REQUIRE_FALSE(fx.cx.simplices[0].empty());
    const Simplex sigma_0 = fx.cx.simplices[0].front();

    CHECK_FALSE(s.get(sigma_0, 0));  // zeros() starts all-clear
    s.set(sigma_0, 0, true);
    CHECK(s.get(sigma_0, 0));
}

// Oracle: cochain.rs::tests::delta_squared_zero_on_triangle
// Triangle of 3 adjacent meshes, all influenced by one light.
TEST_CASE("delta squared is zero on a triangle") {
    Graph g;
    std::array<NodeId, 3> ms{NodeId{}, NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 3; ++i) ms[i] = add_mesh(g);
    const NodeId l = add_light(g);

    REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[0], ms[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[1], ms[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[0], ms[2]).has_value());
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(g.add_edge(EdgeKind::Influences, l, ms[i]).has_value());
    }

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    const BitMatrix d0 = coboundary_matrix(sheaf, cx, 0);
    const BitMatrix d1 = coboundary_matrix(sheaf, cx, 1);
    const BitMatrix composed = d1.mul(d0);
    CHECK(composed.is_zero());  // δ¹ ∘ δ⁰ must vanish
}

// Oracle: tests/cochain.rs::delta_squared_zero_on_k4
// K4 (all 6 pairs adjacent); light 4 → meshes 0,1,2 ; light 5 → meshes 1,2,3.
TEST_CASE("delta squared is zero on K4 with lights") {
    Graph g;
    std::array<NodeId, 4> ms{NodeId{}, NodeId{}, NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 4; ++i) ms[i] = add_mesh(g);
    std::array<NodeId, 2> ls{NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 2; ++i) ls[i] = add_light(g);

    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[i], ms[j]).has_value());
        }
    }
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[0]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[3]).has_value());

    check_delta_squared_is_zero(g);
}

// Oracle: tests/cochain.rs::delta_squared_zero_on_cycle_4
// 4-cycle of meshes; light 4 → mesh 0, light 5 → mesh 2 (alternating).
TEST_CASE("delta squared is zero on a 4-cycle with alternating lights") {
    Graph g;
    std::array<NodeId, 4> ms{NodeId{}, NodeId{}, NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 4; ++i) ms[i] = add_mesh(g);
    std::array<NodeId, 2> ls{NodeId{}, NodeId{}};
    for (std::size_t i = 0; i < 2; ++i) ls[i] = add_light(g);

    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % 4]).has_value());
    }
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[0]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[2]).has_value());

    check_delta_squared_is_zero(g);
}
