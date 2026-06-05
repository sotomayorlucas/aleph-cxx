#include "doctest.h"

#include <algorithm>  // std::count
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

import aleph.math;      // Vec3
import aleph.types;     // NodeId, geometry/material/node payloads
import aleph.graph;     // Graph
import aleph.render.sw; // SceneRT, Face
import aleph.lowering;  // lower, build_sw_scene, SwBuild,
                        // kSphereRings / kSphereSectors

// Phase 6, SPEC §5 test 1 — `build_sw_scene` face counts + face_source map.
//
//   GraphScene ──lower──▶ LoweredScene ──build_sw_scene──▶ {SceneRT, face_source}
//
// `build_sw_scene` rasterizes the frozen IR into the software backend's
// `render::sw::SceneRT` and emits, in parallel with `scene.faces`, a
// `face_source` vector giving the originating graph `NodeId` for each face —
// the pick target the headless `EditorController` resolves against. Per SPEC
// §3.1 each lowered primitive becomes faces:
//   * QuadLocal   -> 2 faces
//   * TriLocal    -> 1 face
//   * SphereLocal -> a low-res, fixed-resolution UV-sphere mesh of
//                    `kSphereRings * kSphereSectors * 2` faces (each
//                    ring×sector cell is a quad split into two faces).
//
// This test lowers a scene with exactly one sphere Mesh and one quad Mesh and
// pins the SPEC §5.1 oracle:
//   * `scene.faces.size() == face_source.size()` (the maps are parallel),
//   * the sphere contributes `kSphereRings * kSphereSectors * 2` faces and the
//     quad contributes 2,
//   * every `face_source[i]` is the correct source entity NodeId (the sphere's
//     faces all carry the sphere's NodeId, the quad's all carry the quad's),
//   * the result is deterministic: a second `build_sw_scene` of the same IR
//     yields a byte-equal face_source map and identical face geometry.
//
// `kSphereRings` / `kSphereSectors` are the fixed tessellation constants the
// bridge exports from `aleph.lowering` (SPEC §3.1: "fixed rings/sectors for
// determinism"); asserting against them — rather than literals — pins the SPEC
// formula while leaving the exact resolution to the implementation.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical 1-sphere + 1-quad enriched graph ──────────────────────────────
// A root Transform (identity) that Contains a Camera, a sphere Mesh and a quad
// Mesh, each referencing its own Lambertian material. No emissive material and
// no Light node — lowering needs only a Camera and resolved References, and we
// want exactly two drawable entities so the per-entity face partition is
// unambiguous.
struct TwoPrims {
    Graph  g;
    NodeId root{}, cam{}, sphere{}, sphere_mat{}, quad{}, quad_mat{};
};

TwoPrims make_two_prims() {
    TwoPrims s;
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

    // Sphere Mesh + its (red) Lambertian.
    s.sphere = g.alloc_node_id();
    Mesh sphere_mesh{s.sphere, std::string("sphere"), 0};
    sphere_mesh.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(sphere_mesh));

    s.sphere_mat = g.alloc_node_id();
    Material smat{s.sphere_mat, MaterialKind::Lambertian};
    smat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    smat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(smat));

    // Quad Mesh + its (green) Lambertian.
    s.quad = g.alloc_node_id();
    Mesh quad_mesh{s.quad, std::string("quad"), 0};
    quad_mesh.geometry = QuadLocal{Vec3{-1, -1, -2}, Vec3{2, 0, 0}, Vec3{0, 2, 0}};
    g.insert_node(std::move(quad_mesh));

    s.quad_mat = g.alloc_node_id();
    Material qmat{s.quad_mat, MaterialKind::Lambertian};
    qmat.albedo = Vec3{0.0f, 1.0f, 0.0f};
    qmat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(qmat));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.sphere);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.quad);
    (void)g.add_edge(EdgeKind::References, s.sphere, s.sphere_mat);
    (void)g.add_edge(EdgeKind::References, s.quad,   s.quad_mat);
    return s;
}

// Count faces in `face_source` whose source is `id`.
[[nodiscard]] std::size_t faces_from(const aleph::lowering::SwBuild& sw, NodeId id) {
    return static_cast<std::size_t>(
        std::count(sw.face_source.begin(), sw.face_source.end(), id));
}

// Bit-equal comparison of two faces' geometry/uv/shade payload (determinism
// oracle). Includes the per-vertex `vcol` so the GOURAUD shade build_sw bakes
// from the scene lights is pinned deterministic too (same LoweredScene =>
// byte-equal baked colours), not just the geometry.
[[nodiscard]] bool same_face(const aleph::render::sw::Face& a,
                             const aleph::render::sw::Face& b) {
    for (std::size_t k = 0; k < 4; ++k) {
        if (!(a.verts[k] == b.verts[k])) return false;
        if (!(a.uvs[k]   == b.uvs[k]))   return false;
        if (!(a.vcol[k]  == b.vcol[k]))  return false;
    }
    return true;
}

}  // namespace

TEST_CASE("build_sw: 1 sphere + 1 quad -> SPEC face counts + face_source map") {
    TwoPrims s = make_two_prims();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    // Pin the IR build_sw_scene consumes: exactly the two drawable entities.
    REQUIRE(lowered->entities.size() == 2);

    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(*lowered);

    // The face_source map is parallel to the emitted faces.
    REQUIRE(sw.face_source.size() == sw.scene.faces.size());

    // ── face counts (SPEC §5.1) ──────────────────────────────────────────────
    const std::size_t sphere_faces =
        static_cast<std::size_t>(aleph::lowering::kSphereRings)
        * static_cast<std::size_t>(aleph::lowering::kSphereSectors) * 2u;
    const std::size_t quad_faces = 2u;

    CHECK(sphere_faces > 0u);
    CHECK(sw.scene.faces.size() == sphere_faces + quad_faces);

    // ── every face_source[i] is the correct source entity NodeId ─────────────
    // The sphere's NodeId owns exactly its tessellation's worth of faces, the
    // quad's owns exactly 2, and NOTHING is sourced from any other node (no
    // dangling / mis-attributed faces).
    CHECK(faces_from(sw, s.sphere) == sphere_faces);
    CHECK(faces_from(sw, s.quad)   == quad_faces);
    CHECK(faces_from(sw, s.sphere) + faces_from(sw, s.quad) == sw.face_source.size());

    for (NodeId src : sw.face_source) {
        const bool is_known = (src == s.sphere) || (src == s.quad);
        CHECK(is_known);
    }
}

TEST_CASE("build_sw: deterministic across two calls") {
    TwoPrims s = make_two_prims();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());

    const aleph::lowering::SwBuild a = aleph::lowering::build_sw_scene(*lowered);
    const aleph::lowering::SwBuild b = aleph::lowering::build_sw_scene(*lowered);

    // Same face_source map, element for element. (Compared element-wise via
    // NodeId::operator== rather than std::vector<NodeId>::operator==: across the
    // module boundary GCC/libstdc++ does not select NodeId's member operator==
    // inside vector's namespace-scope comparison template, so the vector form
    // fails to resolve. Element-wise mirrors the per-face oracle below.)
    REQUIRE(a.face_source.size() == b.face_source.size());
    bool source_equal = true;
    for (std::size_t i = 0; i < a.face_source.size(); ++i) {
        if (!(a.face_source[i] == b.face_source[i])) { source_equal = false; break; }
    }
    CHECK(source_equal);

    // Same emitted geometry, face for face (positions + uvs bit-equal).
    REQUIRE(a.scene.faces.size() == b.scene.faces.size());
    bool all_equal = true;
    for (std::size_t i = 0; i < a.scene.faces.size(); ++i) {
        if (!same_face(a.scene.faces[i], b.scene.faces[i])) { all_equal = false; break; }
    }
    CHECK(all_equal);
}
