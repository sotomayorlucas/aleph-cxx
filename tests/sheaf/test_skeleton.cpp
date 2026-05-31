#include "doctest.h"

import aleph.sheaf;
import aleph.graph;
import aleph.types;

using aleph::graph::Graph;
using aleph::sheaf::OneSkeleton;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::Mesh;
using aleph::types::Node;
using aleph::types::NodeId;

namespace {

NodeId add_mesh(Graph& g, const char* ref) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Node{Mesh{id, ref, 1}});
    return id;
}

}  // namespace

// Oracle: skeleton.rs::empty_graph_yields_empty_skeleton
TEST_CASE("empty graph yields empty skeleton") {
    Graph g;
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    CHECK(skel.vertices.empty());
    CHECK(skel.edges.empty());
}

// Oracle: skeleton.rs::two_adjacent_meshes_yield_one_edge
TEST_CASE("two adjacent meshes yield one edge") {
    Graph g;
    const NodeId a = add_mesh(g, "a");
    const NodeId b = add_mesh(g, "b");
    REQUIRE(g.add_edge(EdgeKind::Adjacent, a, b).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    CHECK(skel.vertices.size() == 2);
    CHECK(skel.edges.size() == 1);
    CHECK(skel.contains_vertex(a));
    CHECK(skel.contains_vertex(b));
    // Canonical (min, max) edge.
    const NodeId lo = (a < b) ? a : b;
    const NodeId hi = (a < b) ? b : a;
    CHECK(skel.contains_edge(lo, hi));
}

// Plan oracle: 4-cycle of meshes -> 4 verts / 4 edges.
TEST_CASE("four cycle of meshes yields four vertices and four edges") {
    Graph g;
    const NodeId a = add_mesh(g, "a");
    const NodeId b = add_mesh(g, "b");
    const NodeId c = add_mesh(g, "c");
    const NodeId d = add_mesh(g, "d");
    REQUIRE(g.add_edge(EdgeKind::Adjacent, a, b).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, b, c).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, c, d).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, d, a).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    CHECK(skel.vertices.size() == 4);
    CHECK(skel.edges.size() == 4);
}

// Plan oracle: non-Adjacent edges are ignored.
// An Influences edge (Light -> Mesh) must not contribute to the skeleton,
// and the Light vertex must not appear (only Mesh vertices are kept).
TEST_CASE("non adjacent edges are ignored") {
    Graph g;
    const NodeId m = add_mesh(g, "m");
    const NodeId lid = g.alloc_node_id();
    g.insert_node(Node{Light{lid, aleph::types::LightKind::Point, "L"}});
    REQUIRE(g.add_edge(EdgeKind::Influences, lid, m).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    CHECK(skel.vertices.size() == 1);  // only the mesh
    CHECK(skel.contains_vertex(m));
    CHECK_FALSE(skel.contains_vertex(lid));
    CHECK(skel.edges.empty());  // Influences edge ignored
}

// Adjacent is symmetric: an edge inserted in either direction is canonicalized
// to (min, max) and not double-counted.
TEST_CASE("adjacent edge canonicalized regardless of insertion direction") {
    Graph g;
    const NodeId a = add_mesh(g, "a");
    const NodeId b = add_mesh(g, "b");
    // Insert in the "reversed" direction (larger id as src).
    const NodeId hi = (a < b) ? b : a;
    const NodeId lo = (a < b) ? a : b;
    REQUIRE(g.add_edge(EdgeKind::Adjacent, hi, lo).has_value());

    const OneSkeleton skel = OneSkeleton::from_graph(g);
    CHECK(skel.edges.size() == 1);
    CHECK(skel.contains_edge(lo, hi));  // canonical (min, max)
}
