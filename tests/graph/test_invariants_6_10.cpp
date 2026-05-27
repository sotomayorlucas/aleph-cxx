#include "doctest.h"
#include <string>
import aleph.graph;
import aleph.types;

using namespace aleph::graph;
using namespace aleph::types;

TEST_CASE("check_camera_exclusive: passes with one camera, fails with zero or two") {
    Graph g;
    {
        auto m = g.alloc_node_id(); g.insert_node(Mesh{m, std::string("m"), 1});
        auto r = check_camera_exclusive(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::CameraExclusive);
    }
    auto c = g.alloc_node_id(); g.insert_node(Camera{c, std::string("cam")});
    CHECK(check_camera_exclusive(g).has_value());
    auto c2 = g.alloc_node_id(); g.insert_node(Camera{c2, std::string("alt")});
    {
        auto r = check_camera_exclusive(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::CameraExclusive);
    }
}

TEST_CASE("check_material_referenced: each Mesh references exactly one Material") {
    Graph g;
    auto mesh = g.alloc_node_id(); g.insert_node(Mesh{mesh, std::string("m"), 1});
    auto mat  = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    {
        auto r = check_material_referenced(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::MaterialReferenced);
    }
    g.add_edge(EdgeKind::References, mesh, mat);
    CHECK(check_material_referenced(g).has_value());
    auto mat2 = g.alloc_node_id(); g.insert_node(Material{mat2, MaterialKind::Metal});
    g.add_edge(EdgeKind::References, mesh, mat2);
    {
        auto r = check_material_referenced(g);
        REQUIRE(!r.has_value());
        CHECK(r.error() == InvariantError::MaterialReferenced);
    }
}

TEST_CASE("check_unique_ids: graph constructor guarantees this (vacuous PASS)") {
    Graph g;
    auto a = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    CHECK(check_unique_ids(g).has_value());
}

TEST_CASE("check_contains_antireflexive: Contains must not be symmetric") {
    Graph g;
    auto t1 = g.alloc_node_id(); g.insert_node(Transform{t1, 0});
    auto t2 = g.alloc_node_id(); g.insert_node(Transform{t2, 1});
    g.add_edge(EdgeKind::Contains, t1, t2);
    CHECK(check_contains_antireflexive(g).has_value());
    g.add_edge(EdgeKind::Contains, t2, t1);
    auto r = check_contains_antireflexive(g);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::ContainsAntireflexive);
}

TEST_CASE("check_bounded_degree: rejects in-degree above limit") {
    Graph g;
    auto mat = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    for (int i = 0; i < 5; ++i) {
        auto m = g.alloc_node_id(); g.insert_node(Mesh{m, std::string("m"), 1});
        g.add_edge(EdgeKind::References, m, mat);
    }
    CHECK(check_bounded_degree(g, 5).has_value());
    auto r = check_bounded_degree(g, 4);
    REQUIRE(!r.has_value());
    CHECK(r.error() == InvariantError::BoundedDegree);
}

TEST_CASE("validate_all: full suite on a well-formed scene") {
    Graph g;
    auto root  = g.alloc_node_id(); g.insert_node(Transform{root, 0});
    auto child = g.alloc_node_id(); g.insert_node(Transform{child, 1});
    auto cam   = g.alloc_node_id(); g.insert_node(Camera{cam, std::string("default")});
    auto light = g.alloc_node_id(); g.insert_node(Light{light, LightKind::Point, std::string("ies/std")});
    auto a     = g.alloc_node_id(); g.insert_node(Mesh{a, std::string("a"), 1});
    auto b     = g.alloc_node_id(); g.insert_node(Mesh{b, std::string("b"), 1});
    auto mat   = g.alloc_node_id(); g.insert_node(Material{mat, MaterialKind::Lambertian});
    auto tex   = g.alloc_node_id(); g.insert_node(Texture{tex, 256, 256, TextureFormat::Rgb8});
    g.add_edge(EdgeKind::Contains,   root,  child);
    g.add_edge(EdgeKind::Contains,   child, a);
    g.add_edge(EdgeKind::Contains,   child, b);
    g.add_edge(EdgeKind::Contains,   root,  cam);
    g.add_edge(EdgeKind::References, a,     mat);
    g.add_edge(EdgeKind::References, b,     mat);
    g.add_edge(EdgeKind::References, mat,   tex);
    g.add_edge(EdgeKind::Influences, light, a);

    auto r = validate_all(g, 64);
    CHECK(r.has_value());
}
