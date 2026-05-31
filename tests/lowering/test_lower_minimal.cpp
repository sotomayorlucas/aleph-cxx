#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

import aleph.math;
import aleph.containers;
import aleph.types;
import aleph.graph;
import aleph.lowering;

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise byte serializers

// SPEC §8.1 — lower_minimal.
//
//   { root Transform -> Camera + Mesh(SphereLocal, Material Lambertian red) + Light }
//
// Oracle:
//   * exactly 1 entity, 1 light, a camera;
//   * handle_map is stable (NodeId -> entities index, the mesh maps to 0);
//   * the LoweredScene is BYTE-IDENTICAL across two independent lower() calls.
//
// LoweredScene holds a move-only OrderedMap (handle_map), so it is not
// copyable / not trivially comparable. We therefore freeze each lowering into
// a flat byte image (POD fields walked in IR iteration order) and compare the
// two images with ==. That is the literal "byte-identical snapshot" the SPEC
// demands, and it also pins insertion order + f32 bit-patterns.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical minimal enriched graph ────────────────────────────────────────
struct Minimal {
    Graph  g;
    NodeId root{}, cam{}, mesh{}, mat{}, light{};
};

Minimal make_minimal() {
    Minimal s;
    Graph& g = s.g;

    // root Transform at identity (no incoming Contains -> a root).
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    // Camera with a concrete pose.
    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // Mesh carrying an analytic sphere payload in local space.
    s.mesh = g.alloc_node_id();
    Mesh mesh{s.mesh, std::string("sphere"), 0};
    mesh.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh));

    // Lambertian red material (NOT emissive -> stays out of the light table).
    s.mat = g.alloc_node_id();
    Material mat{s.mat, MaterialKind::Lambertian};
    mat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat));

    // An explicit Light node — the lone member of the light table here.
    s.light = g.alloc_node_id();
    Light light{s.light, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 2, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    // Hierarchy: root contains the camera, the mesh and the light.
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    // The mesh references its material (must resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh, s.mat);
    return s;
}

// ── byte-image serializer for the frozen IR ─────────────────────────────────
// Provided by lowering_freeze.hpp: leaf-wise, padding-proof. Whole-struct memcpy
// of the IR's Vec3-bearing (alignas(16)) aggregates would capture indeterminate
// padding and fail "byte-identical" CHECKs nondeterministically.
using aleph_test_freeze::freeze;

}  // namespace

TEST_CASE("lowering: lower_minimal -> 1 entity / 1 light / a camera") {
    Minimal s = make_minimal();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    // One renderable entity (the mesh), one light (the explicit Light node).
    CHECK(ls.entities.size() == 1);
    CHECK(ls.lights.size() == 1);

    // The entity is sourced from the mesh and resolves the red Lambertian.
    const aleph::lowering::LoweredEntity& ent = ls.entities[0];
    CHECK(ent.source == s.mesh);
    CHECK(ent.material.kind == MaterialKind::Lambertian);
    CHECK(ent.material.albedo == Vec3{1.0f, 0.0f, 0.0f});
    CHECK(std::holds_alternative<SphereLocal>(ent.world_geometry));

    // Camera was extracted (look_from preserved).
    CHECK(ls.camera.look_from == Vec3{0, 0, 5});
}

TEST_CASE("lowering: handle_map is stable and points the mesh at entity 0") {
    Minimal s = make_minimal();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    // handle_map covers exactly the entities, keyed by source NodeId.
    CHECK(ls.handle_map.size() == ls.entities.size());

    const std::uint32_t* idx = ls.handle_map.get(s.mesh);
    REQUIRE(idx != nullptr);
    CHECK(*idx == 0u);
    CHECK(ls.entities[*idx].source == s.mesh);
}

TEST_CASE("lowering: LoweredScene is byte-identical across two lower() calls") {
    Minimal s = make_minimal();

    auto a = aleph::lowering::lower(s.g);
    auto b = aleph::lowering::lower(s.g);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    const std::vector<std::byte> img_a = freeze(*a);
    const std::vector<std::byte> img_b = freeze(*b);

    REQUIRE(img_a.size() == img_b.size());
    CHECK(img_a == img_b);
}
