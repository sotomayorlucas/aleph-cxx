#include "doctest.h"

#include <algorithm>  // std::count
#include <cmath>      // std::sqrt
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

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
//   * QuadLocal   -> 2*Nu*Nv faces (tessellated into an Nu×Nv cell grid)
//   * TriLocal    -> 1 face
//   * SphereLocal -> a low-res, fixed-resolution UV-sphere mesh of
//                    `kSphereRings * kSphereSectors * 2` faces (each
//                    ring×sector cell is a quad split into two faces).
//
// This test lowers a scene with exactly one sphere Mesh and one quad Mesh and
// pins the SPEC §5.1 oracle:
//   * `scene.faces.size() == face_source.size()` (the maps are parallel),
//   * the sphere contributes `kSphereRings * kSphereSectors * 2` faces and the
//     quad contributes `2 * Nu * Nv` (its tessellation),
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
    // QuadLocal now tessellates into an Nu×Nv grid (2 faces/cell). The test quad
    // has |u|=|v|=2, so with kCell=0.5/kMaxCells=24: Nu=Nv=clamp(ceil(2/0.5))=4
    // => 2·4·4 = 32 faces (mirrors emit_quad's formula).
    const int qNu = 4, qNv = 4;
    const std::size_t quad_faces = static_cast<std::size_t>(2 * qNu * qNv);  // = 32

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

// Task 3 — optional per-entity physics field φ -> colormap vcol.
//
// `build_sw_scene` gains an optional `const std::vector<double>* phi_entity`
// (parallel to `LoweredScene::entities`). When it is null the build is
// BYTE-IDENTICAL to the no-physics path (the determinism oracle `same_face`
// proves vcol is untouched). When non-null, every face of an entity with a φ
// value is tinted with the single diverging colormap colour `colormap(φ)`
// (blue↔white↔red about 0) instead of the baked Lambert shade. Entity 0 is the
// sphere (φ=+1 -> red, red>=blue) and entity 1 the quad (φ=-1 -> blue,
// blue>=red); the sphere's faces lead `scene.faces` and the quad's trail it.
TEST_CASE("build_sw: phi_entity==nullptr is byte-identical; non-null recolors vcol") {
    TwoPrims s = make_two_prims();
    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());

    const aleph::lowering::SwBuild base = aleph::lowering::build_sw_scene(*lowered);
    const aleph::lowering::SwBuild same = aleph::lowering::build_sw_scene(*lowered, nullptr);
    REQUIRE(base.scene.faces.size() == same.scene.faces.size());
    for (std::size_t i = 0; i < base.scene.faces.size(); ++i)
        CHECK(same_face(base.scene.faces[i], same.scene.faces[i]));   // unchanged

    std::vector<double> phi{ +1.0, -1.0 };   // entity 0 red, entity 1 blue
    const aleph::lowering::SwBuild lit = aleph::lowering::build_sw_scene(*lowered, &phi);
    const auto& f0 = lit.scene.faces.front().vcol[0];
    CHECK(f0.x >= f0.z);                                   // red >= blue at φ=+1
    const auto& fl = lit.scene.faces.back().vcol[0];
    CHECK(fl.z >= fl.x);                                   // blue >= red at φ=-1
}

// ── Task 2: analytic contact shadows ────────────────────────────────────────
//
// build_sw bakes per-vertex light occlusion into `Face::vcol`: each light's
// diffuse term is scaled by the fraction of light samples whose segment from the
// shaded point reaches the light unoccluded by the OTHER entities (ambient is
// NOT shadowed). These tests construct a LoweredScene directly (overhead area
// light + a floating sphere + a tessellated floor) so the geometry is exact.

namespace {

// Rec.709-ish luminance of a baked vcol (any vert; a cell is near-flat).
[[nodiscard]] aleph::math::f32 lum(const aleph::math::Vec3& c) {
    return c.x + c.y + c.z;
}

// Centroid of a face's first-three (real) vertices — verts[3]==verts[2].
[[nodiscard]] Vec3 face_centroid(const aleph::render::sw::Face& f) {
    return (f.verts[0] + f.verts[1] + f.verts[2]) * (1.0f / 3.0f);
}

// A direct LoweredScene: overhead area-light quad at y=3, a sphere FLOATING at
// (0,0.8,0) r=0.5, and a big floor quad at y=0 (tessellated by build_sw). The
// floor's grid has a vertex at (0,0,0) directly under the sphere whose segment
// up to the light passes through the sphere => shadowed; a far corner is lit.
aleph::lowering::LoweredScene make_shadow_scene() {
    using aleph::lowering::LoweredEntity;
    using aleph::lowering::MaterialParams;
    aleph::lowering::LoweredScene ls;

    // Overhead area light (id 100): 1x1 quad centred above origin at y=3.
    LoweredEntity light;
    light.source = NodeId{100};
    light.world_geometry = QuadLocal{Vec3{-0.5f, 3.0f, -0.5f}, Vec3{1, 0, 0}, Vec3{0, 0, 1}};
    light.material.emit = Vec3{3.0f, 3.0f, 3.0f};
    ls.lights.push_back(light);

    // Floating sphere occluder (id 1).
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.8f, 0.0f}, 0.5f};
    sphere.material.albedo = Vec3{0.8f, 0.2f, 0.2f};
    ls.entities.push_back(sphere);

    // Floor (id 2): x,z ∈ [-4,4] at y=0, normal up. |u|=|v|=8 => 16x16 cells.
    LoweredEntity floor;
    floor.source = NodeId{2};
    floor.world_geometry = QuadLocal{Vec3{-4.0f, 0.0f, -4.0f}, Vec3{8, 0, 0}, Vec3{0, 0, 8}};
    floor.material.albedo = Vec3{0.7f, 0.7f, 0.7f};
    ls.entities.push_back(floor);

    return ls;
}

}  // namespace

TEST_CASE("build_sw: contact shadow darkens the floor under a sphere") {
    const aleph::lowering::LoweredScene ls = make_shadow_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // Find a floor face nearest the origin (under the sphere) and one far away.
    aleph::math::f32 best_under = 1e30f, lum_under = 0.0f;
    aleph::math::f32 best_far = -1.0f,  lum_far  = 0.0f;
    bool found_under = false, found_far = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{2})) continue;  // floor faces only
        const Vec3 ctr = face_centroid(sw.scene.faces[i]);
        const aleph::math::f32 r = std::sqrt(ctr.x * ctr.x + ctr.z * ctr.z);
        // closest-to-origin floor face -> directly under the sphere
        if (r < best_under) {
            best_under = r;
            lum_under = lum(sw.scene.faces[i].vcol[0]);
            found_under = true;
        }
        // a far floor face (corner region) -> clear path to the light
        if (r > best_far && r > 4.0f) {
            best_far = r;
            lum_far = lum(sw.scene.faces[i].vcol[0]);
            found_far = true;
        }
    }
    REQUIRE(found_under);
    REQUIRE(found_far);
    // The shadowed face under the sphere is measurably darker than the far one.
    CHECK(lum_under < lum_far);
}

TEST_CASE("build_sw: sphere front face stays lit (no false self-shadow)") {
    const aleph::lowering::LoweredScene ls = make_shadow_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // The sphere's topmost vertex faces the overhead light; it must not be
    // wrongly self-shadowed (occluders skip self by NodeId) -> stays bright,
    // brighter than pure ambient (which would be the fully-shadowed floor low).
    aleph::math::f32 top_lum = 0.0f;
    aleph::math::f32 best_y = -1e30f;
    Vec3 top_pt{};
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{1})) continue;  // sphere faces only
        const Vec3 ctr = face_centroid(sw.scene.faces[i]);
        if (ctr.y > best_y) {
            best_y = ctr.y;
            top_lum = lum(sw.scene.faces[i].vcol[0]);
            top_pt = ctr;
        }
    }
    // The sphere is CONVEX and `self` is skipped, so its outward hemisphere is
    // unoccluded by other geometry (the floor is below, the light not an
    // entity) => the sphere's own AO == 1. Assert that explicitly so the
    // ambient seed below is exactly hadamard(base_albedo, sky+sun) (AO doesn't
    // darken it); if AO ever wrongly self-darkened the sphere this would catch it.
    // Unit outward normal (the production caller normalizes before calling AO;
    // onb_from_normal assumes |N|=1, so a raw (unnormalized) centre->surface
    // vector would skew the tangent basis — normalize here too).
    const Vec3 top_normal =
        aleph::math::normalize(top_pt - Vec3{0.0f, 0.8f, 0.0f});  // sphere centre
    const aleph::math::f32 sphere_ao =
        aleph::lowering::detail::ambient_occlusion(top_pt, top_normal, ls.entities, NodeId{1});
    CHECK(sphere_ao == doctest::Approx(1.0f));
    // The lit top must exceed its OWN ambient seed (sky + sun, AO==1, exposed).
    // Sphere (two_sided=false); the shadow scene's sphere albedo is {0.8,0.2,0.2}.
    const Vec3 amb_top = aleph::lowering::detail::sky_ambient(top_normal)
        + aleph::lowering::detail::sun_tint(top_pt, top_normal, ls.lights, /*two_sided=*/false);
    const Vec3 seed_top = aleph::math::hadamard(Vec3{0.8f, 0.2f, 0.2f}, amb_top)
        * aleph::lowering::detail::kRasterExposure;
    CHECK(top_lum > lum(seed_top));  // direct light survives -> the top is genuinely lit
}

TEST_CASE("build_sw: phi override still bypasses shadows/shade") {
    const aleph::lowering::LoweredScene ls = make_shadow_scene();
    // φ for entity 0 (sphere)=+1 (red), entity 1 (floor)=-1 (blue). With φ the
    // vcol must equal the pure colormap colour — proving the φ path skips
    // shade_face/light_visibility entirely (no shadow/Lambert mixed in).
    std::vector<double> phi{ +1.0, -1.0 };
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls, &phi);

    // colormap_diverging(+1, kPhiScale): t=clamp(1/0.4)=1 -> red side {1,0,0}.
    // colormap_diverging(-1, kPhiScale): t=-1 -> blue side {0,0,1}.
    bool checked_sphere = false, checked_floor = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const auto& v = sw.scene.faces[i].vcol[0];
        if (sw.face_source[i] == NodeId{1} && !checked_sphere) {
            CHECK(v.x == doctest::Approx(1.0f));   // red, exactly the colormap
            CHECK(v.y == doctest::Approx(0.0f));
            CHECK(v.z == doctest::Approx(0.0f));
            checked_sphere = true;
        }
        if (sw.face_source[i] == NodeId{2} && !checked_floor) {
            CHECK(v.x == doctest::Approx(0.0f));   // blue, exactly the colormap
            CHECK(v.y == doctest::Approx(0.0f));
            CHECK(v.z == doctest::Approx(1.0f));
            checked_floor = true;
        }
    }
    CHECK(checked_sphere);
    CHECK(checked_floor);
}

// ── Per-vertex ambient occlusion ────────────────────────────────────────────
//
// AO darkens the AMBIENT term where a vertex's hemisphere is occluded by nearby
// geometry. Distinct from the contact-shadow oracle (which needs a light): AO
// darkens the ambient even with NO light, so these scenes carry no lights —
// the only vcol darkening is AO. The key correctness point: the floor's
// cross(u,v)=(0,−64,0) points DOWN; `ambient_occlusion` must reorient to the
// world-up hemisphere internally, else AO samples below the floor and a face
// under the sphere does NOT darken (the oracle would invert).

namespace {

// No-light AO scene: a sphere resting just above a tessellated floor at y=0.
// With NO lights the ambient is the only term, so vcol darkening == AO. The
// sphere centre is at (0,0.55,0) r=0.5 so its bottom (y=0.05) nearly touches
// the floor — a floor face under it sees the sphere block much of its upward
// hemisphere (within kAoDist=2), a far face sees an open hemisphere.
aleph::lowering::LoweredScene make_ao_scene() {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;  // NO lights

    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.55f, 0.0f}, 0.5f};
    sphere.material.albedo = Vec3{0.8f, 0.2f, 0.2f};
    ls.entities.push_back(sphere);

    LoweredEntity floor;
    floor.source = NodeId{2};
    floor.world_geometry = QuadLocal{Vec3{-4.0f, 0.0f, -4.0f}, Vec3{8, 0, 0}, Vec3{0, 0, 8}};
    floor.material.albedo = Vec3{0.7f, 0.7f, 0.7f};
    ls.entities.push_back(floor);

    return ls;
}

}  // namespace

TEST_CASE("build_sw: AO darkens the floor under a sphere (no light)") {
    const aleph::lowering::LoweredScene ls = make_ao_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // Nearest-to-origin floor face (under the sphere) vs the farthest one.
    aleph::math::f32 best_near = 1e30f, lum_near = 0.0f;
    aleph::math::f32 best_far  = -1.0f, lum_far  = 0.0f;
    bool found_near = false, found_far = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{2})) continue;  // floor faces only
        const Vec3 ctr = face_centroid(sw.scene.faces[i]);
        const aleph::math::f32 r = std::sqrt(ctr.x * ctr.x + ctr.z * ctr.z);
        if (r < best_near) { best_near = r; lum_near = lum(sw.scene.faces[i].vcol[0]); found_near = true; }
        if (r > best_far && r > 4.0f) { best_far = r; lum_far = lum(sw.scene.faces[i].vcol[0]); found_far = true; }
    }
    REQUIRE(found_near);
    REQUIRE(found_far);
    // The floor under the sphere is AO-darkened; the far floor's hemisphere is
    // open. If this INVERTS (near brighter), the world-up reorientation is wrong.
    CHECK(lum_near < lum_far);
}

TEST_CASE("build_sw: a lone sphere is not self-darkened (AO == 1 everywhere)") {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;  // NO lights, NO floor — just the sphere
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
    sphere.material.albedo = Vec3{0.8f, 0.2f, 0.2f};
    ls.entities.push_back(sphere);

    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // With no other geometry, every face's AO == 1 (self is skipped, convex).
    // Each face's ambient seed is therefore exactly hadamard(albedo, sky)·exposure,
    // un-darkened and direction-dependent. Recompute per-face and check a match.
    bool checked = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{1})) continue;
        // Verify the AO factor for this vertex is exactly 1 (no occluders).
        // Outward unit normal = normalize(v0 - centre); centre is the origin here.
        const Vec3 v0 = sw.scene.faces[i].verts[0];
        const Vec3 n0 = aleph::math::normalize(v0);
        const aleph::math::f32 ao =
            aleph::lowering::detail::ambient_occlusion(v0, n0, ls.entities, NodeId{1});
        CHECK(ao == doctest::Approx(1.0f));
        // A back-facing (unlit) vertex's vcol is the pure ambient seed (no
        // diffuse with no lights). Directional ambient: the expected lum depends
        // on this face's own normal. No lights -> sun_tint=0; AO==1; centre at
        // origin so n0 == shade_face's N.
        const Vec3 amb = aleph::lowering::detail::sky_ambient(n0);
        const Vec3 expect =
            aleph::math::hadamard(sphere.material.albedo, amb)
            * aleph::lowering::detail::kRasterExposure;
        CHECK(lum(sw.scene.faces[i].vcol[0]) == doctest::Approx(lum(expect)));
        checked = true;
        break;
    }
    CHECK(checked);
}

// ── Directional sky ambient (hemispheric) ───────────────────────────────────
//
// The flat grey ambient (previously a constant 0.45) is replaced by a hemispheric sky term
// (cool/bright at the zenith, neutral/dimmer at the horizon) sampled by the
// world-up-reoriented normal. With NO lights the ambient IS the sky term, so a
// neutral sphere's vcol reads the sky gradient directly: an up-facing (top)
// face must be both brighter AND bluer than an equator (horizon) face.

namespace {

// Neutral-albedo sphere at the origin, NO lights -> ambient is sky only.
// Albedo.z>0 is REQUIRED: the blue-fraction oracle measures albedo.z·amb.z; a
// zero-blue (red) albedo collapses it to 0>0 (spurious fail).
aleph::lowering::LoweredScene make_sky_scene() {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;  // NO lights -> ambient is sky only
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
    sphere.material.albedo = Vec3{0.7f, 0.7f, 0.7f};   // neutral: albedo.z>0
    ls.entities.push_back(sphere);
    return ls;
}

}  // namespace

TEST_CASE("build_sw: hemispheric sky ambient — up-faces are brighter and bluer") {
    const aleph::lowering::LoweredScene ls = make_sky_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);

    // Sphere normal at a face = normalize(centroid - centre); centre is the origin.
    // Pick the most up-facing (top, N.y->+1 -> zenith) and a near-equator face
    // (|N.y| smallest -> horizon). Compare top vs EQUATOR (NOT bottom: the world-up
    // flip makes bottom==top).
    aleph::math::f32 top_ny = -1.0f, eq_ny = 2.0f;
    Vec3 top_c{}, eq_c{};
    bool found_top = false, found_eq = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const Vec3 c = face_centroid(sw.scene.faces[i]);
        const Vec3 N = aleph::math::normalize(c);  // centre at origin
        if (N.y > top_ny) { top_ny = N.y; top_c = sw.scene.faces[i].vcol[0]; found_top = true; }
        if (std::fabs(N.y) < eq_ny) { eq_ny = std::fabs(N.y); eq_c = sw.scene.faces[i].vcol[0]; found_eq = true; }
    }
    REQUIRE(found_top);
    REQUIRE(found_eq);
    // Brighter (albedo-agnostic) AND bluer (needs albedo.z>0) at the zenith.
    CHECK(lum(top_c) > lum(eq_c));
    CHECK(top_c.z / lum(top_c) > eq_c.z / lum(eq_c));
}

// ── Directional sun tint (warm half-Lambert fill) ───────────────────────────
//
// A soft warm fill wraps from the dominant light's direction. On a SPHERE
// (two_sided=false) a shadowed-hemisphere vertex (dot(N,L)<=0) skips the direct
// light, so its vcol is ambient-only — sky + sun_tint. Among equator vertices
// (same N.y => identical sky_ambient, so the vertical gradient cancels), the
// one more toward the light gets the larger warm wrap and reads warmer (higher
// red fraction). A quad/tri would be two_sided=true (ndl=|ndl0|) and leak ~2.6×
// neutral direct light onto the away face, swamping the warm-fraction signal.

TEST_CASE("build_sw: sun tint warms the equator vertices facing the light") {
    using aleph::lowering::LoweredEntity;
    aleph::lowering::LoweredScene ls;
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
    sphere.material.albedo = Vec3{0.7f, 0.7f, 0.7f};   // neutral
    ls.entities.push_back(sphere);
    LoweredEntity light;                               // off to +X
    light.source = NodeId{100};
    light.world_geometry = QuadLocal{Vec3{5.0f, -0.5f, -0.5f}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    light.material.emit = Vec3{3.0f, 3.0f, 3.0f};
    ls.lights.push_back(light);

    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);
    const Vec3 Lc = Vec3{5.0f, 0.0f, 0.0f};  // light centre (for dot(N,L) classification)

    // Classify by the SHADED vertex's OWN normal (verts[0]), NOT the face
    // centroid: vcol[0] is baked at verts[0], whose ring may differ from the
    // centroid's. Gating on n0 keeps both chosen vertices on the SAME equator
    // ring (identical sky_ambient.y -> the vertical gradient cancels), isolating
    // the sun-tint signal. Among shadowed (dot(n0,L)<=0 -> direct light skipped
    // on the two_sided=false sphere) near-equator vertices (|n0.y|<0.15), pick
    // the one most-toward the light (max dot, grazing) and most-away (min dot).
    aleph::math::f32 toward_dot = -2.0f, away_dot = 2.0f;
    Vec3 toward_c{}, away_c{};
    bool found_t = false, found_a = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const Vec3 v0 = sw.scene.faces[i].verts[0];
        const Vec3 n0 = aleph::math::normalize(v0);          // centre at origin
        if (std::fabs(n0.y) > 0.15f) continue;               // equator ring only
        const Vec3 L = aleph::math::normalize(Lc - v0);
        const aleph::math::f32 nl = aleph::math::dot(n0, L);
        if (nl > 0.0f) continue;                             // shadowed side only
        if (nl > toward_dot) { toward_dot = nl; toward_c = sw.scene.faces[i].vcol[0]; found_t = true; }
        if (nl < away_dot)   { away_dot   = nl; away_c   = sw.scene.faces[i].vcol[0]; found_a = true; }
    }
    REQUIRE(found_t);
    REQUIRE(found_a);
    REQUIRE(toward_dot <= 0.0f);   // both genuinely shadowed -> ambient-only vcol
    // The vertex more toward the light gets the larger warm wrap -> higher red fraction.
    CHECK(toward_c.x / lum(toward_c) > away_c.x / lum(away_c));
}
