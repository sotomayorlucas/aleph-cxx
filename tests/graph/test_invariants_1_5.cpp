#include "doctest.h"
#include <string>

import aleph.graph;
import aleph.types;

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("INVARIANT_NAMES: 10 canonical names in spec order") {
    CHECK(INVARIANT_NAMES.size() == 10);
    CHECK(INVARIANT_NAMES[0] == "TypedNodes");
    CHECK(INVARIANT_NAMES[1] == "TypedEdges");
    CHECK(INVARIANT_NAMES[2] == "EdgeEndpointsExist");
    CHECK(INVARIANT_NAMES[3] == "EdgeTypeCompat");
    CHECK(INVARIANT_NAMES[4] == "TransformAcyclic");
}

TEST_CASE("check_typed_nodes / check_typed_edges: vacuously true on empty graph") {
    Graph g;
    CHECK(check_typed_nodes(g).has_value());
    CHECK(check_typed_edges(g).has_value());
}

TEST_CASE("check_edge_endpoints_exist: passes when all endpoints present") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    g.add_edge(EdgeKind::Adjacent, a, b);
    CHECK(check_edge_endpoints_exist(g).has_value());
}

TEST_CASE("check_edge_type_compat: passes on a well-typed graph") {
    Graph g;
    auto a   = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b   = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    g.add_edge(EdgeKind::Adjacent,   a, b);
    g.add_edge(EdgeKind::References, a, mat);
    CHECK(check_edge_type_compat(g).has_value());
}

TEST_CASE("check_transform_acyclic: detects Contains cycle among Transforms") {
    Graph g;
    auto t1 = g.alloc_node_id(); g.insert_node(Transform{t1, 0});
    auto t2 = g.alloc_node_id(); g.insert_node(Transform{t2, 1});
    auto t3 = g.alloc_node_id(); g.insert_node(Transform{t3, 2});
    g.add_edge(EdgeKind::Contains, t1, t2);
    g.add_edge(EdgeKind::Contains, t2, t3);
    g.add_edge(EdgeKind::Contains, t3, t1);
    auto r = check_transform_acyclic(g);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::TransformAcyclic);
}

TEST_CASE("check_transform_acyclic: passes on a tree") {
    Graph g;
    auto root  = g.alloc_node_id(); g.insert_node(Transform{root, 0});
    auto left  = g.alloc_node_id(); g.insert_node(Transform{left, 1});
    auto right = g.alloc_node_id(); g.insert_node(Transform{right, 2});
    (void)g.add_edge(EdgeKind::Contains, root, left);
    (void)g.add_edge(EdgeKind::Contains, root, right);
    CHECK(check_transform_acyclic(g).has_value());
}
