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
using aleph::sheaf::FlagComplex;
using aleph::sheaf::LightSet;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::Simplex;
using aleph::sheaf::VisibilitySheaf;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::lights_influencing;
using aleph::sheaf::make_simplex;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::LightKind;
using aleph::types::MediumKind;
using aleph::types::Mesh;
using aleph::types::Node;
using aleph::types::NodeId;
using aleph::types::Volume;

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

// Count of set bits in row `r` of `m` (the row's popcount). An inclusion
// matrix has exactly one set bit per row.
std::size_t row_popcount(const BitMatrix& m, std::size_t r) {
    std::size_t n = 0;
    for (std::size_t c = 0; c < m.cols(); ++c) {
        if (m.at(r, c)) ++n;
    }
    return n;
}

}  // namespace

// Oracle: sheaf.rs::vertex_stalk_lists_influencing_lights
// One Light influencing one Mesh -> the stalk lists exactly that light.
TEST_CASE("vertex stalk lists influencing lights") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());

    const LightSet lights = lights_influencing(g, m);
    CHECK(lights.size() == 1);
    // membership: the light id is present
    bool has_l = false;
    for (const NodeId id : lights) {
        if (id == l) has_l = true;
    }
    CHECK(has_l);
}

// Only Light-kind sources count: a Volume —Influences→ Mesh edge (allowed by
// the edge typing rules) must NOT contribute to the visibility light set.
TEST_CASE("non-light influences source is excluded from the light set") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    const NodeId vol = g.alloc_node_id();
    g.insert_node(Node{Volume{vol, MediumKind::Homogeneous}});
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, vol, m).has_value());

    const LightSet lights = lights_influencing(g, m);
    CHECK(lights.size() == 1);  // only the Light, not the Volume
    CHECK(lights[0] == l);
}

// Oracle: sheaf.rs::visibility_sheaf_implements_trait
// dim_stalk == 1, and restriction(sigma, sigma) is the 1x1 identity.
TEST_CASE("visibility sheaf implements the cellular Z2 sheaf surface") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    const Simplex sigma = make_simplex({m});
    CHECK(sheaf.dim_stalk(sigma) == 1);

    const BitMatrix r = sheaf.restriction(sigma, sigma);
    CHECK(r.rows() == 1);
    CHECK(r.cols() == 1);
    CHECK(r.at(0, 0));
}

// Missing simplices have a 0-dimensional stalk (Rust map_or(0, len)).
TEST_CASE("dim_stalk of an absent simplex is zero") {
    Graph g;
    const NodeId m = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    // A simplex never present in this 1-vertex complex.
    const Simplex absent = make_simplex({NodeId{999}});
    CHECK(sheaf.dim_stalk(absent) == 0);
}

// Oracle fixture: cochain.rs::k4_with_lights.
//   meshes ms = {0,1,2,3}, lights ls = {4,5}; K4 adjacency (all pairs).
//   Light 4 -> mesh 0,1,2 ;  Light 5 -> mesh 1,2,3.
// Hand-computed stalks (intersection of vertex light sets):
//   F({0})       = {4}        dim 1
//   F({1})       = {4,5}      dim 2
//   F({2})       = {4,5}      dim 2
//   F({3})       = {5}        dim 1
//   F({0,1})     = {4}        dim 1
//   F({0,2})     = {4}        dim 1
//   F({0,3})     = {}         dim 0
//   F({1,2})     = {4,5}      dim 2
//   F({1,3})     = {5}        dim 1
//   F({2,3})     = {5}        dim 1
//   F({0,1,2})   = {4}        dim 1
//   F({0,1,3})   = {}         dim 0
//   F({0,2,3})   = {}         dim 0
//   F({1,2,3})   = {5}        dim 1
//   F({0,1,2,3}) = {}         dim 0
TEST_CASE("k4 with lights: stalk dimensions match by hand") {
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
    // Light 0 -> mesh 0,1,2 ; Light 1 -> mesh 1,2,3.
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[0]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[0], ms[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, ls[1], ms[3]).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    // Vertices.
    CHECK(sheaf.dim_stalk(make_simplex({ms[0]})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({ms[1]})) == 2);
    CHECK(sheaf.dim_stalk(make_simplex({ms[2]})) == 2);
    CHECK(sheaf.dim_stalk(make_simplex({ms[3]})) == 1);

    // Edges.
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[1]})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[2]})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[3]})) == 0);
    CHECK(sheaf.dim_stalk(make_simplex({ms[1], ms[2]})) == 2);
    CHECK(sheaf.dim_stalk(make_simplex({ms[1], ms[3]})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({ms[2], ms[3]})) == 1);

    // Triangles.
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[1], ms[2]})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[1], ms[3]})) == 0);
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[2], ms[3]})) == 0);
    CHECK(sheaf.dim_stalk(make_simplex({ms[1], ms[2], ms[3]})) == 1);

    // Tetrahedron.
    CHECK(sheaf.dim_stalk(make_simplex({ms[0], ms[1], ms[2], ms[3]})) == 0);
}

// Restriction is the 0/1 inclusion of F(tau) into F(sigma): dim_stalk(tau)
// rows x dim_stalk(sigma) cols, with exactly one set bit per row (each light
// of F(tau) maps to its index in F(sigma)). Checked on the K4 fixture.
TEST_CASE("k4 with lights: restriction is a 0/1 inclusion matrix") {
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

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    // Face {1} (F = {4,5}) ⊂ cofacet {1,2} (F = {4,5}). The restriction is the
    // 2x2 inclusion: both lights present in both stalks at the same indices,
    // so it is the identity.
    {
        const Simplex sigma = make_simplex({ms[1]});
        const Simplex tau = make_simplex({ms[1], ms[2]});
        const BitMatrix r = sheaf.restriction(sigma, tau);
        CHECK(r.rows() == 2);  // dim F(tau)
        CHECK(r.cols() == 2);  // dim F(sigma)
        CHECK(row_popcount(r, 0) == 1);
        CHECK(row_popcount(r, 1) == 1);
        CHECK(r.at(0, 0));  // light 4 -> index 0
        CHECK(r.at(1, 1));  // light 5 -> index 1
    }

    // Face {1} (F = {4,5}) ⊂ cofacet {1,3} (F = {5}). The single light of
    // F(tau) is light 5, which sits at index 1 of F(sigma) = {4,5}.
    {
        const Simplex sigma = make_simplex({ms[1]});
        const Simplex tau = make_simplex({ms[1], ms[3]});
        const BitMatrix r = sheaf.restriction(sigma, tau);
        CHECK(r.rows() == 1);  // dim F(tau) = 1
        CHECK(r.cols() == 2);  // dim F(sigma) = 2
        CHECK(row_popcount(r, 0) == 1);
        CHECK_FALSE(r.at(0, 0));  // not light 4
        CHECK(r.at(0, 1));        // light 5 at index 1 of {4,5}
    }

    // Face {0} (F = {4}) ⊂ cofacet {0,3} (F = {} empty). The restriction has
    // zero rows (empty target stalk) and one column.
    {
        const Simplex sigma = make_simplex({ms[0]});
        const Simplex tau = make_simplex({ms[0], ms[3]});
        const BitMatrix r = sheaf.restriction(sigma, tau);
        CHECK(r.rows() == 0);  // dim F(tau) = 0
        CHECK(r.cols() == 1);  // dim F(sigma) = 1
    }
}

// build_1_skeleton_only populates 0- and 1-simplex stalks only, with the same
// intersection semantics on edges. (cochain.rs uses this for the M2 union-find
// fast path.) Two adjacent meshes with distinct lights -> empty edge stalk.
TEST_CASE("build_1_skeleton_only: distinct lights yield an empty edge stalk") {
    Graph g;
    const NodeId m0 = add_mesh(g);
    const NodeId m1 = add_mesh(g);
    const NodeId l0 = add_light(g);
    const NodeId l1 = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Adjacent, m0, m1).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l0, m0).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l1, m1).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_1_skeleton_only(g, skel);

    CHECK(sheaf.dim_stalk(make_simplex({m0})) == 1);
    CHECK(sheaf.dim_stalk(make_simplex({m1})) == 1);
    // F({m0,m1}) = {l0} ∩ {l1} = {} since the lights are distinct.
    CHECK(sheaf.dim_stalk(make_simplex({m0, m1})) == 0);
}

// build_1_skeleton_only: a shared light survives the edge intersection.
TEST_CASE("build_1_skeleton_only: shared light survives the edge intersection") {
    Graph g;
    const NodeId m0 = add_mesh(g);
    const NodeId m1 = add_mesh(g);
    const NodeId l = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Adjacent, m0, m1).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m0).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l, m1).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_1_skeleton_only(g, skel);

    const Simplex edge = make_simplex({m0, m1});
    CHECK(sheaf.dim_stalk(edge) == 1);
    CHECK(sheaf.stalk_at(edge).size() == 1);
    CHECK(sheaf.stalk_at(edge)[0] == l);
}

// stalk_at returns the sorted light set; lights are ascending by NodeId
// regardless of edge-insertion order. Mirrors the Rust `out.sort()`.
TEST_CASE("stalk_at returns lights sorted ascending by NodeId") {
    Graph g;
    const NodeId m = add_mesh(g);
    // Allocate two lights; influence the mesh with the *higher* id first so the
    // sort is observable.
    const NodeId l0 = add_light(g);
    const NodeId l1 = add_light(g);
    REQUIRE(g.add_edge(EdgeKind::Influences, l1, m).has_value());
    REQUIRE(g.add_edge(EdgeKind::Influences, l0, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    const FlagComplex cx = build_flag_complex(skel);
    const VisibilitySheaf sheaf = VisibilitySheaf::build_full(g, cx);

    const LightSet& s = sheaf.stalk_at(make_simplex({m}));
    REQUIRE(s.size() == 2);
    CHECK(s[0] == l0);  // lower id first
    CHECK(s[1] == l1);
    CHECK(s[0] < s[1]);
}
