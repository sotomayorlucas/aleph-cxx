#include "doctest.h"

#include <string>
#include <utility>

import aleph.graph;
import aleph.types;
import aleph.sheaf;

using aleph::graph::Graph;
using aleph::sheaf::Subgraph;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// Allocate a Mesh node and return its id.
NodeId add_mesh(Graph& g, const char* geo) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Mesh{id, std::string(geo), 1});
    return id;
}

// Add an Adjacent edge a—b, returning its runtime EdgeId.
aleph::types::EdgeId add_adjacent(Graph& g, NodeId a, NodeId b) {
    auto r = g.add_edge(EdgeKind::Adjacent, a, b);
    REQUIRE(r.has_value());
    return *r;
}

}  // namespace

TEST_CASE("empty subgraph induces empty skeleton and complex") {
    Graph g;
    add_mesh(g, "a");
    add_mesh(g, "b");
    const Subgraph sg;  // selects nothing

    auto skel = sg.one_skeleton(g);
    CHECK(skel.vertices.empty());
    CHECK(skel.edges.empty());

    auto [skel2, cx] = sg.flag_complex(g);
    CHECK(skel2.vertices.empty());
    // No vertices selected => no simplices in any present level.
    for (const auto& level : cx.simplices) {
        CHECK(level.empty());
    }
}

TEST_CASE("singleton subgraph induces one vertex, no edges, max_dim 0") {
    Graph g;
    const NodeId m = add_mesh(g, "x");
    add_mesh(g, "y");  // present in host but NOT selected

    Subgraph sg;
    sg.node_ids.insert(m);

    auto [skel, cx] = sg.flag_complex(g);
    CHECK(skel.vertices.size() == 1);
    CHECK(skel.contains_vertex(m));
    CHECK(skel.edges.empty());
    // build_flag_complex of a single vertex: max_dim 0, exactly one 0-simplex.
    CHECK(cx.max_dim == 0);
    REQUIRE(cx.simplices.size() >= 1);
    CHECK(cx.simplices[0].size() == 1);
}

TEST_CASE("edge requires both endpoints selected AND its edge id selected") {
    Graph g;
    const NodeId a = add_mesh(g, "a");
    const NodeId b = add_mesh(g, "b");
    const auto eid = add_adjacent(g, a, b);

    SUBCASE("both endpoints + edge selected => one induced edge") {
        Subgraph sg;
        sg.node_ids.insert(a);
        sg.node_ids.insert(b);
        sg.edge_ids.insert(eid);
        auto skel = sg.one_skeleton(g);
        CHECK(skel.vertices.size() == 2);
        CHECK(skel.edges.size() == 1);
    }

    SUBCASE("endpoints selected but edge id NOT selected => no edge") {
        Subgraph sg;
        sg.node_ids.insert(a);
        sg.node_ids.insert(b);
        // edge_ids deliberately empty
        auto skel = sg.one_skeleton(g);
        CHECK(skel.vertices.size() == 2);
        CHECK(skel.edges.empty());
    }

    SUBCASE("edge selected but only one endpoint selected => no edge") {
        Subgraph sg;
        sg.node_ids.insert(a);  // b omitted
        sg.edge_ids.insert(eid);
        auto skel = sg.one_skeleton(g);
        CHECK(skel.vertices.size() == 1);
        CHECK(skel.edges.empty());
    }
}

TEST_CASE("non-Adjacent edges are never lifted into the skeleton") {
    // A Light influencing a Mesh: Influences edge, both nodes exist, but the
    // 1-skeleton only carries Adjacent edges between Mesh vertices.
    Graph g;
    const NodeId m = add_mesh(g, "m");
    const NodeId l = g.alloc_node_id();
    g.insert_node(aleph::types::Light{
        l, aleph::types::LightKind::Point, std::string("e")});
    auto r = g.add_edge(EdgeKind::Influences, l, m);
    REQUIRE(r.has_value());

    Subgraph sg;
    sg.node_ids.insert(m);
    sg.node_ids.insert(l);  // even if the light is "selected"...
    sg.edge_ids.insert(*r);

    auto skel = sg.one_skeleton(g);
    // ...only the Mesh vertex survives, and the Influences edge is dropped.
    CHECK(skel.vertices.size() == 1);
    CHECK(skel.contains_vertex(m));
    CHECK(!skel.contains_vertex(l));
    CHECK(skel.edges.empty());
}

TEST_CASE("induced triangle => flag complex max_dim 2 with 3/3/1 simplices") {
    // Oracle from flag_complex.rs::build_triangle_yields_max_dim_two_with_seven_simplices.
    Graph g;
    const NodeId v0 = add_mesh(g, "0");
    const NodeId v1 = add_mesh(g, "1");
    const NodeId v2 = add_mesh(g, "2");
    const auto e01 = add_adjacent(g, v0, v1);
    const auto e12 = add_adjacent(g, v1, v2);
    const auto e02 = add_adjacent(g, v0, v2);

    Subgraph sg;
    sg.node_ids.insert(v0);
    sg.node_ids.insert(v1);
    sg.node_ids.insert(v2);
    sg.edge_ids.insert(e01);
    sg.edge_ids.insert(e12);
    sg.edge_ids.insert(e02);

    auto [skel, cx] = sg.flag_complex(g);
    CHECK(skel.vertices.size() == 3);
    CHECK(skel.edges.size() == 3);
    CHECK(cx.max_dim == 2);
    REQUIRE(cx.simplices.size() == 3);
    CHECK(cx.simplices[0].size() == 3);  // 3 vertices
    CHECK(cx.simplices[1].size() == 3);  // 3 edges
    CHECK(cx.simplices[2].size() == 1);  // 1 triangle
}

TEST_CASE("dropping one triangle edge collapses the 2-simplex (no clique)") {
    // Same vertices/edges as the triangle, but the subgraph withholds one
    // edge id => 1-skeleton is a path, no 3-clique, no 2-simplex.
    Graph g;
    const NodeId v0 = add_mesh(g, "0");
    const NodeId v1 = add_mesh(g, "1");
    const NodeId v2 = add_mesh(g, "2");
    const auto e01 = add_adjacent(g, v0, v1);
    const auto e12 = add_adjacent(g, v1, v2);
    add_adjacent(g, v0, v2);  // exists in host, withheld from the subgraph

    Subgraph sg;
    sg.node_ids.insert(v0);
    sg.node_ids.insert(v1);
    sg.node_ids.insert(v2);
    sg.edge_ids.insert(e01);
    sg.edge_ids.insert(e12);

    auto [skel, cx] = sg.flag_complex(g);
    CHECK(skel.vertices.size() == 3);
    CHECK(skel.edges.size() == 2);
    CHECK(cx.max_dim == 1);
    REQUIRE(cx.simplices.size() >= 2);
    CHECK(cx.simplices[0].size() == 3);
    CHECK(cx.simplices[1].size() == 2);
    // No 2-simplices: level absent or empty.
    if (cx.simplices.size() > 2) {
        CHECK(cx.simplices[2].empty());
    }
}

TEST_CASE("induced K4 => flag complex max_dim 3 with 4/6/4/1 simplices") {
    // Oracle from flag_complex.rs::build_k4_yields_max_dim_three_with_fifteen_simplices.
    Graph g;
    NodeId v[4];
    for (int i = 0; i < 4; ++i) v[i] = add_mesh(g, "k");

    Subgraph sg;
    for (int i = 0; i < 4; ++i) sg.node_ids.insert(v[i]);
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            sg.edge_ids.insert(add_adjacent(g, v[i], v[j]));
        }
    }

    auto [skel, cx] = sg.flag_complex(g);
    CHECK(skel.vertices.size() == 4);
    CHECK(skel.edges.size() == 6);
    CHECK(cx.max_dim == 3);
    REQUIRE(cx.simplices.size() == 4);
    CHECK(cx.simplices[0].size() == 4);   // vertices
    CHECK(cx.simplices[1].size() == 6);   // edges
    CHECK(cx.simplices[2].size() == 4);   // triangles
    CHECK(cx.simplices[3].size() == 1);   // tetrahedron
}

TEST_CASE("induced 4-cycle => max_dim 1, no 2-simplex") {
    // Oracle from flag_complex.rs::build_4_cycle_yields_max_dim_one.
    Graph g;
    NodeId v[4];
    for (int i = 0; i < 4; ++i) v[i] = add_mesh(g, "c");
    const auto e0 = add_adjacent(g, v[0], v[1]);
    const auto e1 = add_adjacent(g, v[1], v[2]);
    const auto e2 = add_adjacent(g, v[2], v[3]);
    const auto e3 = add_adjacent(g, v[3], v[0]);

    Subgraph sg;
    for (int i = 0; i < 4; ++i) sg.node_ids.insert(v[i]);
    sg.edge_ids.insert(e0);
    sg.edge_ids.insert(e1);
    sg.edge_ids.insert(e2);
    sg.edge_ids.insert(e3);

    auto [skel, cx] = sg.flag_complex(g);
    CHECK(skel.vertices.size() == 4);
    CHECK(skel.edges.size() == 4);
    CHECK(cx.max_dim == 1);
    REQUIRE(cx.simplices.size() >= 2);
    CHECK(cx.simplices[0].size() == 4);
    CHECK(cx.simplices[1].size() == 4);
    if (cx.simplices.size() > 2) {
        CHECK(cx.simplices[2].empty());
    }
}

TEST_CASE("Subgraph is copyable (value-type cover piece)") {
    // MV decomposition clones U/K/R; verify the value-type contract.
    Graph g;
    const NodeId a = add_mesh(g, "a");
    const NodeId b = add_mesh(g, "b");
    const auto eid = add_adjacent(g, a, b);

    Subgraph orig;
    orig.node_ids.insert(a);
    orig.node_ids.insert(b);
    orig.edge_ids.insert(eid);

    Subgraph clone = orig;  // copy must compile and be independent
    CHECK(clone.node_ids.contains(a));
    CHECK(clone.node_ids.contains(b));
    CHECK(clone.edge_ids.contains(eid));

    auto skel = clone.one_skeleton(g);
    CHECK(skel.vertices.size() == 2);
    CHECK(skel.edges.size() == 1);
}
