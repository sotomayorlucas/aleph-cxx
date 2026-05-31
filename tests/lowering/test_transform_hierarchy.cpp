#include "doctest.h"

#include <string>
#include <variant>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;

// SPEC §8.2 — transform_hierarchy.
//
// Nested Transforms compose a hand-computed world position. The lowering walks
// Contains in insertion order, composing world = parent.world * node.local.m
// (SPEC §4.2 step 2), then transforms each Mesh's GeometryPayload into world
// space. We assert the world-space sphere center equals a center we work out
// by hand.
//
// Hierarchy (all pure translations so the math stays trivial and exact in f32):
//
//   root  Transform  T(10, 0, 0)
//     └── mid  Transform  T(0, 5, 0)
//           └── leaf Transform  T(0, 0, -2)   contains  mesh
//
//   mesh  SphereLocal{ center = (1, 2, 3), r = 0.5 }
//
//   world_matrix = T(10,0,0) * T(0,5,0) * T(0,0,-2) = T(10, 5, -2)
//   world_center = world_matrix * (1, 2, 3, 1) = (11, 7, 1)
//
// Translations compose by addition and commute, so the col-major Mat4 product
// gives exactly T(10, 5, -2); applying it to the local center yields (11,7,1)
// with no rounding.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::math::Mat4;

namespace {

LocalTransform translate(float x, float y, float z) {
    return LocalTransform{Mat4::translate(Vec3{x, y, z})};
}

}  // namespace

TEST_CASE("lowering: nested transforms compose a hand-computed world position") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, translate(10, 0, 0)});

    const NodeId mid = g.alloc_node_id();
    g.insert_node(Transform{mid, 1, translate(0, 5, 0)});

    const NodeId leaf = g.alloc_node_id();
    g.insert_node(Transform{leaf, 2, translate(0, 0, -2)});

    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("sphere"), 0};
    m.geometry = SphereLocal{Vec3{1, 2, 3}, 0.5f};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material material{mat, MaterialKind::Lambertian};
    material.albedo = Vec3{0.2f, 0.4f, 0.6f};
    g.insert_node(std::move(material));

    // A camera so the scene lowers without NoCamera; parented under root.
    const NodeId cam = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("sensor0")});

    (void)g.add_edge(EdgeKind::Contains,   root, mid);
    (void)g.add_edge(EdgeKind::Contains,   mid,  leaf);
    (void)g.add_edge(EdgeKind::Contains,   leaf, mesh);
    (void)g.add_edge(EdgeKind::Contains,   root, cam);
    (void)g.add_edge(EdgeKind::References, mesh, mat);

    auto lowered = aleph::lowering::lower(g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    REQUIRE(ls.entities.size() == 1);
    const aleph::lowering::LoweredEntity& ent = ls.entities[0];
    CHECK(ent.source == mesh);

    REQUIRE(std::holds_alternative<SphereLocal>(ent.world_geometry));
    const SphereLocal& world = std::get<SphereLocal>(ent.world_geometry);

    // Hand-computed world center (1,2,3) under T(10,5,-2) = (11,7,1).
    CHECK(world.center.x == doctest::Approx(11.0f));
    CHECK(world.center.y == doctest::Approx(7.0f));
    CHECK(world.center.z == doctest::Approx(1.0f));
    // A pure translation must not touch the radius.
    CHECK(world.radius == doctest::Approx(0.5f));
}
