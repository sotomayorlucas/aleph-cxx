#include "doctest.h"
#include <string>
#include <vector>

import aleph.graph;
import aleph.types;

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("Graph: empty by default") {
    Graph g;
    CHECK(g.node_count() == 0);
    CHECK(g.edge_count() == 0);
}

TEST_CASE("Graph: alloc_node_id is monotonic and independent of alloc_edge_id") {
    Graph g;
    CHECK(g.alloc_node_id().value == 0);
    CHECK(g.alloc_node_id().value == 1);
    CHECK(g.alloc_edge_id().value == 0);
    CHECK(g.alloc_node_id().value == 2);
}

TEST_CASE("Graph: insert_node + lookup") {
    Graph g;
    auto id = g.alloc_node_id();
    g.insert_node(Mesh{id, std::string("cube"), 12});
    CHECK(g.node_count() == 1);
    const Node* n = g.node(id);
    REQUIRE(n != nullptr);
    CHECK(kind_of(*n) == NodeKind::Mesh);
    CHECK(id_of(*n) == id);
}

TEST_CASE("Graph: node returns nullptr on miss") {
    Graph g;
    CHECK(g.node(NodeId{999}) == nullptr);
}

TEST_CASE("Graph: nodes() iteration is insertion order") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Material{b, MaterialKind::Lambertian});
    auto c = g.alloc_node_id(); g.insert_node(Camera{c, std::string("cam")});

    std::vector<NodeId> ids;
    for (auto [id, node] : g.nodes()) ids.push_back(id);
    CHECK(ids.size() == 3);
    CHECK(ids[0] == a);
    CHECK(ids[1] == b);
    CHECK(ids[2] == c);
}

TEST_CASE("Graph: remove_node_cascade removes node (edges in task 10)") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 2});
    g.remove_node_cascade(a);
    CHECK(g.node_count() == 1);
    CHECK(g.node(a) == nullptr);
    CHECK(g.node(b) != nullptr);
}
