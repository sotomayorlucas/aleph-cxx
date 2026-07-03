#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.lowering;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

struct Seed {
    Graph  g;
    NodeId root{}, cam{};
};

Seed make_seed() {
    Seed s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);
    return s;
}

std::vector<std::byte> bytes_of(const char* text) {
    std::vector<std::byte> out;
    while (*text != '\0') {
        out.push_back(static_cast<std::byte>(*text));
        ++text;
    }
    return out;
}

std::vector<std::byte> one_triangle_obj() {
    return bytes_of(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
}

aleph::lowering::MaterialParams imported_material() {
    aleph::lowering::MaterialParams m{};
    m.kind     = MaterialKind::TexturedLambertian;
    m.albedo   = Vec3{0.25f, 0.5f, 0.75f};
    m.fuzz     = 0.0f;
    m.ior      = 1.5f;
    m.emit     = Vec3{0, 0, 0};
    m.uv_scale = 7.0f;
    return m;
}

}  // namespace

TEST_CASE("lowering: ImportObj creates one TriLocal entity and shared material") {
    Seed s = make_seed();
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = one_triangle_obj();
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE(applied.has_value());

    CHECK(s.g.node_count() == nodes_before + 3);  // group Transform + Material + Mesh
    CHECK(s.g.edge_count() == edges_before + 3);  // root->group, group->mesh, mesh->mat
    REQUIRE(applied->created_nodes.size() == 3);
    REQUIRE(applied->created_edges.size() == 3);

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    REQUIRE(lowered->entities.size() == 1);

    const aleph::lowering::LoweredEntity& e = lowered->entities[0];
    CHECK(e.material.kind == MaterialKind::TexturedLambertian);
    CHECK(e.material.albedo == Vec3{0.25f, 0.5f, 0.75f});
    CHECK(e.material.uv_scale == doctest::Approx(7.0f));
    REQUIRE(std::holds_alternative<TriLocal>(e.world_geometry));
    const TriLocal& tri = std::get<TriLocal>(e.world_geometry);
    CHECK(tri.a == Vec3{0, 0, 0});
    CHECK(tri.b == Vec3{1, 0, 0});
    CHECK(tri.c == Vec3{0, 1, 0});
}

TEST_CASE("lowering: ImportObj invalid OBJ rolls back graph") {
    Seed s = make_seed();
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = bytes_of("f 1 2 3\n");
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::InvariantViolation);
    CHECK(s.g.node_count() == nodes_before);
    CHECK(s.g.edge_count() == edges_before);
}

TEST_CASE("lowering: ImportObj validates parent") {
    Seed s = make_seed();

    aleph::lowering::ImportObj missing{};
    missing.parent    = NodeId{999999};
    missing.obj_bytes = one_triangle_obj();
    missing.material  = imported_material();
    auto no_parent = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(missing)});
    REQUIRE_FALSE(no_parent.has_value());
    CHECK(no_parent.error() == aleph::lowering::OpError::NodeNotFound);

    aleph::lowering::ImportObj wrong_kind{};
    wrong_kind.parent    = s.cam;
    wrong_kind.obj_bytes = one_triangle_obj();
    wrong_kind.material  = imported_material();
    auto not_transform = aleph::lowering::apply_op(
        s.g, aleph::lowering::Op{std::move(wrong_kind)});
    REQUIRE_FALSE(not_transform.has_value());
    CHECK(not_transform.error() == aleph::lowering::OpError::KindMismatch);
}

TEST_CASE("lowering: ImportObj rejects triangle cap overflow") {
    Seed s = make_seed();
    std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n";
    for (int i = 0; i < 4097; ++i) {
        obj += "f 1 2 3\n";
    }

    aleph::lowering::ImportObj imp{};
    imp.parent    = s.root;
    imp.obj_bytes = bytes_of(obj.c_str());
    imp.material  = imported_material();

    auto applied = aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)});
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::InvariantViolation);
}
