#include "doctest.h"
#include <string>
#include <vector>

import aleph.graph;
import aleph.types;

using namespace aleph::graph;
using namespace aleph::types;

static auto make_basic_two_mesh() {
    struct Out { Graph g; NodeId a; NodeId b; NodeId mat; };
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    return Out{std::move(g), a, b, mat};
}

TEST_CASE("Graph::add_edge: success on compatible types") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto res = g.add_edge(EdgeKind::Adjacent, a, b);
    REQUIRE(res.has_value());
    EdgeId e = res.value();
    CHECK(g.edge_count() == 1);
    const Edge* ep = g.edge(e);
    REQUIRE(ep != nullptr);
    CHECK(ep->kind == EdgeKind::Adjacent);
    CHECK(ep->src == a);
    CHECK(ep->dst == b);
}

TEST_CASE("Graph::add_edge: rejects incompatible types") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto res = g.add_edge(EdgeKind::Adjacent, a, mat);
    REQUIRE(!res.has_value());
    REQUIRE(res.error() == GraphError::EdgeTypeMismatch);
    CHECK(g.edge_count() == 0);
}

TEST_CASE("Graph::add_edge: rejects unknown src/dst") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto res = g.add_edge(EdgeKind::Adjacent, a, NodeId{999});
    REQUIRE(!res.has_value());
    REQUIRE(res.error() == GraphError::NodeNotFound);
}

TEST_CASE("Graph::edges() iterates in insertion order") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e1 = g.add_edge(EdgeKind::References, a, mat).value();
    auto e2 = g.add_edge(EdgeKind::References, b, mat).value();
    auto e3 = g.add_edge(EdgeKind::Adjacent,   a, b).value();

    std::vector<EdgeId> ids;
    for (auto [id, e] : g.edges()) ids.push_back(id);
    CHECK(ids.size() == 3);
    CHECK(ids[0] == e1);
    CHECK(ids[1] == e2);
    CHECK(ids[2] == e3);
}

TEST_CASE("Graph::remove_node_cascade: cascades incident edges") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    auto e1 = g.add_edge(EdgeKind::References, a, mat).value();
    auto e2 = g.add_edge(EdgeKind::References, b, mat).value();
    auto e3 = g.add_edge(EdgeKind::Adjacent,   a, b).value();
    CHECK(g.edge_count() == 3);

    g.remove_node_cascade(a);
    CHECK(g.node_count() == 2);
    CHECK(g.edge_count() == 1);
    CHECK(g.edge(e1) == nullptr);
    CHECK(g.edge(e2) != nullptr);
    CHECK(g.edge(e3) == nullptr);
}

TEST_CASE("Graph::in_degree: counts incoming edges by dst") {
    auto [g, a, b, mat] = make_basic_two_mesh();
    (void)g.add_edge(EdgeKind::References, a, mat);
    (void)g.add_edge(EdgeKind::References, b, mat);
    CHECK(g.in_degree(mat) == 2);
    CHECK(g.in_degree(a)   == 0);
}
