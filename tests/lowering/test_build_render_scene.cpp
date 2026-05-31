#include "doctest.h"

#include <string>
#include <variant>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.scene;
import aleph.lowering;

// SPEC §8.3 — build_render_scene.
//
// The functor chain is  GraphScene ──lower──▶ LoweredScene ──build──▶ RenderScene.
// Here we exercise the final, "thin translation only" hop (SPEC §4.3):
//
//   build_render_scene(LoweredScene) -> RenderScene
//
// Oracle: lower the canonical minimal scene, build the RenderScene, and assert
// it contains EXACTLY:
//   * 1 sphere      (the lone Mesh, carrying a SphereLocal payload),
//   * 1 lambertian  (the red Lambertian the Mesh References),
//   * 1 light       (the explicit Light node, surfaced as an emissive primitive),
//   * the camera     (the unique Camera's pose, forwarded verbatim).
//
// build_render_scene performs NO decisions (SPEC §4.3): it walks the frozen IR,
// emitting one `scene_add_{sphere,quad,tri}` per entity with a material picked by
// `scene_add_{lambertian,metal,dielectric,emissive}` on MaterialParams::kind, the
// same for each light-table member, forwards the camera pose, and builds the BVH.
// The Scene (SoA + BVH) holds no camera, so the build result bundles the camera
// pose alongside the Scene — that bundle is `aleph::lowering::RenderScene` and is
// what we assert on. We never touch renderer internals beyond the public SoA
// counts the SPEC names.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical minimal enriched graph ────────────────────────────────────────
// Identical in spirit to test_lower_minimal's fixture: one root Transform that
// Contains a Camera, a Mesh (SphereLocal + red Lambertian via References) and an
// explicit Light node. The Mesh's material is NOT emissive, so the only light is
// the Light node — giving exactly 1 entity and exactly 1 light after lowering.
struct Minimal {
    Graph  g;
    NodeId root{}, cam{}, mesh{}, mat{}, light{};
};

Minimal make_minimal() {
    Minimal s;
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

    s.mesh = g.alloc_node_id();
    Mesh mesh{s.mesh, std::string("sphere"), 0};
    mesh.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh));

    s.mat = g.alloc_node_id();
    Material mat{s.mat, MaterialKind::Lambertian};
    mat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat));

    s.light = g.alloc_node_id();
    Light light{s.light, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 2, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    (void)g.add_edge(EdgeKind::References, s.mesh, s.mat);
    return s;
}

}  // namespace

TEST_CASE("lowering: build_render_scene -> 1 sphere / 1 lambertian / 1 light / camera") {
    Minimal s = make_minimal();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());

    // Sanity-pin the IR the build consumes: exactly one entity and one light.
    REQUIRE(lowered->entities.size() == 1);
    REQUIRE(lowered->lights.size() == 1);

    const aleph::lowering::RenderScene rs =
        aleph::lowering::build_render_scene(*lowered);
    const aleph::scene::Scene& scene = rs.scene;

    // ── exactly 1 sphere, and nothing else geometric ────────────────────────
    CHECK(scene.spheres.cx.size() == 1);
    CHECK(scene.spheres.r.size() == 1);
    // The lone Mesh lowers to a unit sphere at the origin (identity transform).
    CHECK(scene.spheres.cx[0] == doctest::Approx(0.0f));
    CHECK(scene.spheres.cy[0] == doctest::Approx(0.0f));
    CHECK(scene.spheres.cz[0] == doctest::Approx(0.0f));
    CHECK(scene.spheres.r[0]  == doctest::Approx(1.0f));
    CHECK(scene.tris.v0x.size() == 0);

    // ── exactly 1 lambertian ─────────────────────────────────────────────────
    CHECK(scene.lamb.albedo.size() == 1);
    CHECK(scene.lamb.albedo[0] == Vec3{1.0f, 0.0f, 0.0f});
    // The entity's material is NOT metal/dielectric (only the light is emissive).
    CHECK(scene.metal.albedo.size() == 0);
    CHECK(scene.diel.ior.size() == 0);
    // The sphere binds the lone Lambertian material slot.
    REQUIRE(scene.spheres.mat.size() == 1);
    CHECK(scene.spheres.mat[0].kind == aleph::scene::MaterialKind::Lambertian);
    CHECK(scene.spheres.mat[0].idx == 0u);

    // ── exactly 1 light ──────────────────────────────────────────────────────
    // The Light node is surfaced as an emissive primitive (a quad here) and
    // registered in the Scene light list by scene_add_*.
    CHECK(scene.lights.size() == 1);
    CHECK(scene.emis.emit.size() == 1);
    CHECK(scene.emis.emit[0] == Vec3{4, 4, 4});
    CHECK(scene.quads.Qx.size() == 1);

    // ── the camera ───────────────────────────────────────────────────────────
    // build_render_scene forwards the unique Camera's pose verbatim (no
    // projection math — that is the renderer's job, parameterized by image size).
    CHECK(rs.camera.look_from == Vec3{0, 0, 5});
    CHECK(rs.camera.look_at   == Vec3{0, 0, 0});
    CHECK(rs.camera.up        == Vec3{0, 1, 0});
    CHECK(rs.camera.vfov_deg  == doctest::Approx(40.0f));

    // ── BVH built over the one primitive set (SPEC §4.3: scene_build_bvh) ────
    // 1 sphere + 1 quad = 2 primitives -> at least one internal node.
    CHECK(scene.bvh.nodes.size() >= 1);
}
