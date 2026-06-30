#include "doctest.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unistd.h>
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

std::filesystem::path unique_project_path(const char* name) {
    return std::filesystem::temp_directory_path()
        / (std::string("aleph_") + name + "_" + std::to_string(::getpid()) + ".aleph");
}

}  // namespace

TEST_CASE("project workflow: import OBJ, save graph, load graph, lower imported tri") {
    Seed s = make_seed();

    aleph::lowering::MaterialParams mat{};
    mat.kind     = MaterialKind::Lambertian;
    mat.albedo   = Vec3{0.6f, 0.7f, 0.8f};
    mat.uv_scale = 4.0f;

    aleph::lowering::ImportObj imp{};
    imp.parent = s.root;
    imp.obj_bytes = bytes_of(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    imp.material = mat;
    REQUIRE(aleph::lowering::apply_op(s.g, aleph::lowering::Op{std::move(imp)}).has_value());

    const std::filesystem::path path = unique_project_path("project_workflow");
    REQUIRE(aleph::graph::save_graph_file(s.g, s.root, path.string()).has_value());
    auto loaded = aleph::graph::load_graph_file(path.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->root == s.root);

    auto lowered = aleph::lowering::lower(loaded->graph);
    REQUIRE(lowered.has_value());
    REQUIRE(lowered->entities.size() == 1);
    CHECK(lowered->entities[0].material.albedo == Vec3{0.6f, 0.7f, 0.8f});
    CHECK(lowered->entities[0].material.uv_scale == doctest::Approx(4.0f));
    CHECK(std::holds_alternative<TriLocal>(lowered->entities[0].world_geometry));
    const TriLocal& tri = std::get<TriLocal>(lowered->entities[0].world_geometry);
    CHECK(tri.a == Vec3{0, 0, 0});
    CHECK(tri.b == Vec3{1, 0, 0});
    CHECK(tri.c == Vec3{0, 1, 0});

    std::filesystem::remove(path);
}
