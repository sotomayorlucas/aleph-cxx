#include "doctest.h"

#include <algorithm>  // std::count
#include <cmath>      // std::sqrt
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>    // std::pair (oracle 3 peak/neighbour)
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

    // Polish slice 4d: pin the finer-sphere target so an accidental down-tune is
    // caught, plus a geometric "rounder than before" floor (old 12 rings ≈0.262 rad).
    CHECK(aleph::lowering::kSphereRings   >= 20);
    CHECK(aleph::lowering::kSphereSectors >= 28);
    const double polar_step = 3.14159265358979323846 /
                              static_cast<double>(aleph::lowering::kSphereRings);
    CHECK(polar_step <= 0.18);   // 20 rings ≈0.157 passes; old 12 rings ≈0.262 fails

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

TEST_CASE("build_sw: softer shadows resolve 1/16-step penumbra visibility (4x4 grid)") {
    using aleph::math::f32;
    const aleph::lowering::LoweredScene ls = make_shadow_scene();
    const aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(ls);
    REQUIRE(ls.lights.size() == 1u);
    // The 4x4 (16-sample) area grid yields penumbra visibility in 1/16ths — a value
    // the old 2x2 (quarter-step) grid could not produce. Probe light_visibility at
    // floor-face vertices and assert at least one genuine 16th-but-not-quarter step
    // (e.g. the penumbra corner's 10/16 = 0.625). Direct granularity proof; no
    // dependence on luminance/atten or how many cells fall in the penumbra.
    bool found_sixteenth = false;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        if (!(sw.face_source[i] == NodeId{2})) continue;             // floor faces
        const Vec3 p = sw.scene.faces[i].verts[0];
        const f32 v = aleph::lowering::detail::light_visibility(
            p, Vec3{0.0f, 1.0f, 0.0f}, ls.lights[0], ls.entities, NodeId{2});
        if (v <= 0.001f || v >= 0.999f) continue;                    // penumbra only
        const f32 s16 = v * 16.0f, s4 = v * 4.0f;
        if (std::fabs(s16 - std::round(s16)) < 1e-3f &&              // a 16th-step
            std::fabs(s4  - std::round(s4))  > 1e-3f) {              // not a quarter-step
            found_sixteenth = true; break;
        }
    }
    CHECK(found_sixteenth);
    CHECK(aleph::lowering::detail::kShadowSamples >= 4);
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

// ── Task 4c-i: material-kind raster shading oracles ─────────────────────────
//
// build_sw now shades by MaterialKind. The eye is threaded through the bake
// (`build_sw_scene(ls, eye, phi)`); Metal/Dielectric are view-dependent. These
// oracles pin (1) the Lambert branch byte-for-byte (a GOLDEN, operator==), and
// (2..4) the Metal / Dielectric shading RELATIONALLY (env-reflection spread, a
// pinned specular vertex, the Fresnel rim + dim center). Per SPEC §5 the eye
// sits OUTSIDE the sphere ({0,0,5}, r=1) so the V=N guard is never the path.

namespace {

using aleph::lowering::LoweredEntity;
using aleph::lowering::MaterialParams;
using aleph::math::f32;

// VERBATIM copy of shade_face's Lambertian/Emissive branch (build_sw.cppm — the
// `git show 8aa9ef7` Lambert path). The whole point of the golden is byte
// identity, so this MUST reproduce the f32 expression tree exactly (normalize N;
// ao; amb = sky_ambient + sun_tint; lit = hadamard(albedo,amb)*ao + emit; the
// per-light loop; *kRasterExposure). If `==` ever fails, Task 1's Lambert branch
// has DIVERGED — a real regression to surface, NOT a reason to switch to Approx.
// SYNC POINT: if build_sw.cppm shade_face's Lambertian/Emissive branch (the tail
// of shade_face, ~lines 571-585) is ever changed, update this copy verbatim in
// lockstep — otherwise the golden silently drifts with the implementation.
[[nodiscard]] Vec3 shade_lambert_ref(Vec3 point, Vec3 normal, Vec3 albedo, Vec3 emit,
                                     const std::vector<LoweredEntity>& lights,
                                     bool two_sided,
                                     const std::vector<LoweredEntity>& occluders,
                                     NodeId self) {
    namespace d = aleph::lowering::detail;
    const Vec3 base_albedo = albedo;
    const Vec3 self_emit   = emit;
    const f32  nlen = aleph::math::length(normal);
    const Vec3 N = (nlen > 1e-8f) ? normal * (1.0f / nlen)
                                  : Vec3{0.0f, 1.0f, 0.0f};
    const f32  ao = d::ambient_occlusion(point, N, occluders, self);
    const Vec3 amb = d::sky_ambient(N) + d::sun_tint(point, N, lights, two_sided);
    Vec3 lit = aleph::math::hadamard(base_albedo, amb) * ao + self_emit;
    for (const LoweredEntity& L : lights) {
        const Vec3 dd = d::light_center(L.world_geometry) - point;
        const f32  dist_sq = aleph::math::dot(dd, dd);
        if (dist_sq < 1e-6f) continue;
        const f32  ndl0 = aleph::math::dot(N, dd) / std::sqrt(dist_sq);
        const f32  ndl = two_sided ? std::fabs(ndl0) : (ndl0 > 0.0f ? ndl0 : 0.0f);
        if (ndl <= 0.0f) continue;
        const f32  atten = 1.0f / (1.0f + d::kFall * dist_sq);
        const f32  vis = d::light_visibility(point, N, L, occluders, self);
        lit = lit + aleph::math::hadamard(base_albedo, L.material.emit)
                        * (ndl * atten * d::kLightScale * vis);
    }
    return lit * d::kRasterExposure;
}

// A direct LoweredScene with a single sphere of a chosen material at the origin
// (r=1), eye {0,0,5}, optionally a white Lambert reference sphere / no lights.
aleph::lowering::LoweredScene make_material_scene(aleph::types::MaterialKind kind,
                                                  Vec3 albedo, f32 fuzz, f32 ior) {
    aleph::lowering::LoweredScene ls;
    ls.camera.look_from = Vec3{0, 0, 5};
    LoweredEntity sphere;
    sphere.source = NodeId{1};
    sphere.world_geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    sphere.material.kind   = kind;
    sphere.material.albedo = albedo;
    sphere.material.fuzz   = fuzz;
    sphere.material.ior    = ior;
    ls.entities.push_back(sphere);
    return ls;
}

}  // namespace

// 1. Lambert byte-identity GOLDEN (operator==, NOT Approx).
//
// For an all-Lambertian scene assert every baked vcol[k] EXACTLY equals
// shade_lambert_ref(verts[k], normal_at_k, ...). The per-vertex normal +
// two_sided MUST match how emit_* calls shade_face: floor (quad) = cross(u,v),
// two_sided=true; sphere = vert-center, two_sided=false. This pins the f32 tree
// byte-for-byte — the only construction that catches a Lambert-branch divergence
// the relational oracles cannot.
TEST_CASE("build_sw: Lambertian branch is byte-identical to the reference shade") {
    const aleph::lowering::LoweredScene ls = make_shadow_scene();  // all Lambertian
    // Use the eye-overload (Lambert ignores eye; pass the scene camera explicitly).
    const aleph::lowering::SwBuild sw =
        aleph::lowering::build_sw_scene(ls, ls.camera.look_from);

    const Vec3 sphere_center{0.0f, 0.8f, 0.0f};                 // make_shadow_scene
    const Vec3 sphere_albedo{0.8f, 0.2f, 0.2f};
    const Vec3 floor_normal =
        aleph::math::cross(Vec3{8, 0, 0}, Vec3{0, 0, 8});        // floor's cross(u,v)
    const Vec3 floor_albedo{0.7f, 0.7f, 0.7f};
    const Vec3 emit0{0.0f, 0.0f, 0.0f};

    std::size_t checked_sphere = 0, checked_floor = 0, mismatches = 0;
    for (std::size_t i = 0; i < sw.scene.faces.size(); ++i) {
        const aleph::render::sw::Face& f = sw.scene.faces[i];
        const NodeId src = sw.face_source[i];
        const bool   is_sphere = (src == NodeId{1});
        for (std::size_t k = 0; k < 3; ++k) {                   // verts[3]==verts[2]
            const Vec3 vk = f.verts[k];
            const Vec3 nk = is_sphere ? (vk - sphere_center) : floor_normal;
            const Vec3 ref = shade_lambert_ref(
                vk, nk, is_sphere ? sphere_albedo : floor_albedo, emit0,
                ls.lights, /*two_sided=*/!is_sphere, ls.entities, src);
            const Vec3 got = f.vcol[k];
            // operator== (channelwise) — byte-identity, NOT Approx.
            const bool eq = (got.x == ref.x) && (got.y == ref.y) && (got.z == ref.z);
            CHECK(eq);
            if (!eq) ++mismatches;
        }
        if (is_sphere) ++checked_sphere; else ++checked_floor;
    }
    CHECK(checked_sphere > 0);
    CHECK(checked_floor > 0);
    CHECK(mismatches == 0);   // byte-for-byte; any non-zero is a Lambert regression
}

// 2. Metal env-reflection: eye {0,0,5}, NO lights. A Metal sphere vs a Lambert
// sphere of the SAME albedo. Among front-facing faces (dot(N,V)>0): metal's
// per-vertex luminance SPREAD (max-min) exceeds lambert's (the signed sky<->ground
// ramp), AND the front-facing vertex with min R.y (most-downward reflection ->
// dark ground) is darker than the one with max R.y (up -> bright zenith). Under a
// world-up-folded ambient those would be equal — proves the SIGNED env term.
TEST_CASE("build_sw: Metal env-reflection has a signed sky<->ground gradient") {
    const Vec3 albedo{0.85f, 0.85f, 0.9f};
    const Vec3 eye{0, 0, 5};
    const aleph::lowering::LoweredScene metal_ls =
        make_material_scene(aleph::types::MaterialKind::Metal, albedo, 0.05f, 1.5f);
    const aleph::lowering::LoweredScene lamb_ls =
        make_material_scene(aleph::types::MaterialKind::Lambertian, albedo, 0.0f, 1.5f);
    const aleph::lowering::SwBuild m = aleph::lowering::build_sw_scene(metal_ls, eye);
    const aleph::lowering::SwBuild l = aleph::lowering::build_sw_scene(lamb_ls, eye);
    REQUIRE(m.scene.faces.size() == l.scene.faces.size());

    f32 m_min = 1e30f, m_max = -1e30f, l_min = 1e30f, l_max = -1e30f;
    f32 lo_Ry = 2.0f,  hi_Ry = -2.0f, lum_lo_Ry = 0.0f, lum_hi_Ry = 0.0f;
    bool found = false;
    for (std::size_t i = 0; i < m.scene.faces.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 vk = m.scene.faces[i].verts[k];
            const Vec3 N = aleph::math::normalize(vk);          // center at origin
            const Vec3 ev = eye - vk;
            const Vec3 V = ev * (1.0f / aleph::math::length(ev));
            if (aleph::math::dot(N, V) <= 0.0f) continue;       // front-facing only
            found = true;
            const f32 ml = lum(m.scene.faces[i].vcol[k]);
            const f32 ll = lum(l.scene.faces[i].vcol[k]);
            m_min = std::min(m_min, ml); m_max = std::max(m_max, ml);
            l_min = std::min(l_min, ll); l_max = std::max(l_max, ll);
            // R = reflect(-V, N).
            const Vec3 R = aleph::math::reflect(V * -1.0f, N);
            if (R.y < lo_Ry) { lo_Ry = R.y; lum_lo_Ry = ml; }
            if (R.y > hi_Ry) { hi_Ry = R.y; lum_hi_Ry = ml; }
        }
    }
    REQUIRE(found);
    const f32 metal_spread = m_max - m_min;
    const f32 lamb_spread  = l_max - l_min;
    MESSAGE("metal spread=" << metal_spread << " lambert spread=" << lamb_spread);
    MESSAGE("min-R.y lum=" << lum_lo_Ry << " (R.y=" << lo_Ry
            << ")  max-R.y lum=" << lum_hi_Ry << " (R.y=" << hi_Ry << ")");
    CHECK(metal_spread > lamb_spread);          // signed env ramp > flat ambient
    CHECK(lum_lo_Ry < lum_hi_Ry);               // down-reflection (ground) darker
}

// 3. Metal specular sharpness via fuzz: eye {0,0,5}. Pick a front-facing sphere
// vertex normal N0; place a light so H=normalize(L+V)=N0 at that vertex
// (Ldir = 2*dot(V,N0)*N0 - V; center = N0_point + Ldir*4). The highlight LANDS
// (peak lum >> no-light env). Un-normalized Blinn-Phong has pow(1,p)=1 at the
// exact peak, so the fuzz0/fuzz1 PEAKS are equal by construction; the
// fuzz->sharpness property shows OFF the peak: the sharp lobe (fuzz0) falls off
// faster, so the broad lobe (fuzz1) is brighter at the neighbour vertex.
TEST_CASE("build_sw: Metal specular highlight lands; fuzz broadens it off the peak") {
    const Vec3 albedo{0.85f, 0.85f, 0.9f};
    const Vec3 eye{0, 0, 5};

    // Pick a real on_sphere vertex normal: ring/sector giving a front, off-axis
    // facet. theta = pi*ring/RINGS, phi = 2pi*sector/SECTORS; vertex (r=1) =
    // (sin th cos ph, cos th, sin th sin ph). Choose the equator-ish facet facing
    // partly toward +Z (front) and a little +X so the highlight is off the pole.
    const f32 kPi = 3.14159265358979323846f;
    const int ring = aleph::lowering::kSphereRings / 2 - 2;   // above equator, front
    const int sector = aleph::lowering::kSphereSectors / 4;   // toward +Z (sin>0)
    const f32 th = kPi * static_cast<f32>(ring) / static_cast<f32>(aleph::lowering::kSphereRings);
    const f32 ph = 2.0f * kPi * static_cast<f32>(sector) / static_cast<f32>(aleph::lowering::kSphereSectors);
    const Vec3 N0{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
    const Vec3 N0_point = N0;                                  // r=1, center origin
    const Vec3 ev = eye - N0_point;
    const Vec3 V = ev * (1.0f / aleph::math::length(ev));
    REQUIRE(aleph::math::dot(N0, V) > 0.0f);                   // front-facing

    // Ldir so the half-vector at this vertex aligns with N0.
    const Vec3 Ldir = N0 * (2.0f * aleph::math::dot(V, N0)) - V;
    const Vec3 light_center = N0_point + Ldir * 4.0f;
    // Guard: dot(N0, normalize(normalize(center-point)+V)) ≈ 1 (H == N0).
    const Vec3 Lhat = aleph::math::normalize(light_center - N0_point);
    const Vec3 H = aleph::math::normalize(Lhat + V);
    REQUIRE(aleph::math::dot(N0, H) == doctest::Approx(1.0f).epsilon(0.01));

    auto build_with = [&](f32 fuzz, bool with_light) {
        aleph::lowering::LoweredScene ls =
            make_material_scene(aleph::types::MaterialKind::Metal, albedo, fuzz, 1.5f);
        if (with_light) {
            LoweredEntity light;
            light.source = NodeId{100};
            light.world_geometry =
                SphereLocal{light_center, 0.1f};                // point-ish light
            light.material.emit = Vec3{3.0f, 3.0f, 3.0f};
            ls.lights.push_back(light);
        }
        return aleph::lowering::build_sw_scene(ls, eye);
    };

    // Find the baked vertex whose normal best matches N0 (the peak) AND the
    // second-best (the off-peak neighbour); read both lums. The peak's dot==1 so
    // pow(1,shininess)==1 for ANY exponent — the sharpness shows only OFF peak.
    auto lums = [&](const aleph::lowering::SwBuild& sw) -> std::pair<f32, f32> {
        f32 best = -2.0f, lum_best = 0.0f, second = -2.0f, lum_second = 0.0f;
        for (std::size_t i = 0; i < sw.scene.faces.size(); ++i)
            for (std::size_t k = 0; k < 3; ++k) {
                const Vec3 n = aleph::math::normalize(sw.scene.faces[i].verts[k]);
                const f32 d = aleph::math::dot(n, N0);
                const f32 L = lum(sw.scene.faces[i].vcol[k]);
                if (d > best) { second = best; lum_second = lum_best; best = d; lum_best = L; }
                else if (d > second && d < best - 1e-5f) { second = d; lum_second = L; }
            }
        REQUIRE(best == doctest::Approx(1.0f).epsilon(0.001));  // the exact vertex exists
        return {lum_best, lum_second};
    };

    const auto [lum_fuzz0, neigh_fuzz0] = lums(build_with(0.0f, /*with_light=*/true));
    const auto [lum_fuzz1, neigh_fuzz1] = lums(build_with(1.0f, /*with_light=*/true));
    const auto [lum_nolight, _]         = lums(build_with(0.0f, /*with_light=*/false));
    MESSAGE("pinned vertex lum: fuzz0=" << lum_fuzz0 << " fuzz1=" << lum_fuzz1
            << " no-light=" << lum_nolight);
    MESSAGE("off-peak neighbour lum: fuzz0=" << neigh_fuzz0 << " fuzz1=" << neigh_fuzz1
            << "  (sharp lobe falls off faster => fuzz0 neighbour < fuzz1 neighbour)");
    CHECK(lum_fuzz0 > lum_nolight);                 // the highlight genuinely adds light (lands)
    CHECK(lum_fuzz0 == doctest::Approx(lum_fuzz1));  // un-normalized peak: pow(1,p)=1 for any p
    CHECK(neigh_fuzz0 < neigh_fuzz1);               // sharp lobe falls off faster => fuzz1 broader off-peak
}

// 4. Dielectric Fresnel rim + dark center: eye {0,0,5}, neutral albedo (<=1/ch).
// (a) the grazing vertex (min |dot(N,V)|, F->1->kRimColor) is brighter than the
// face-on vertex (max |dot(N,V)|, F->r0->kGlassCenter).
// (b) the glass face-on vertex (max |dot(N,V)|, |N.y|<0.1 — horizon, worst case)
// is DARKER than a white Lambert sphere's face-on vertex (the real "dim center"
// discriminator: kGlassCenter sum 0.77 << a white horizon Lambert ~0.86+).
TEST_CASE("build_sw: Dielectric has a bright Fresnel rim over a dim center") {
    const Vec3 eye{0, 0, 5};
    // Neutral albedo <= 1/ch (1/3): keeps the kGlassAlbedoMix center tint small so
    // the dim-center discriminator holds (a high albedo would brighten the center
    // term enough to exceed a white-Lambert horizon — SPEC §5 "neutral albedo ≤1/ch").
    const Vec3 glass_albedo{0.3f, 0.3f, 0.3f};
    const aleph::lowering::LoweredScene glass_ls =
        make_material_scene(aleph::types::MaterialKind::Dielectric, glass_albedo, 0.0f, 1.5f);
    const aleph::lowering::SwBuild g = aleph::lowering::build_sw_scene(glass_ls, eye);

    // (a) grazing (min |N.V|) brighter than face-on (max |N.V|), front-facing.
    f32 min_nv = 2.0f, max_nv = -1.0f, lum_grazing = 0.0f, lum_faceon = 0.0f;
    // (b) face-on near the horizon (|N.y|<0.1) for the dim-center comparison.
    f32 max_nv_h = -1.0f, lum_faceon_h = 0.0f;
    for (std::size_t i = 0; i < g.scene.faces.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 vk = g.scene.faces[i].verts[k];
            const Vec3 N = aleph::math::normalize(vk);
            const Vec3 ev = eye - vk;
            const Vec3 V = ev * (1.0f / aleph::math::length(ev));
            const f32 nv = aleph::math::dot(N, V);
            if (nv <= 0.0f) continue;                          // front-facing
            const f32 anv = std::fabs(nv);
            const f32 L = lum(g.scene.faces[i].vcol[k]);
            if (anv < min_nv) { min_nv = anv; lum_grazing = L; }
            if (anv > max_nv) { max_nv = anv; lum_faceon  = L; }
            if (std::fabs(N.y) < 0.1f && anv > max_nv_h) { max_nv_h = anv; lum_faceon_h = L; }
        }
    REQUIRE(min_nv <= 1.0f);
    REQUIRE(max_nv_h > 0.0f);                                   // a horizon face-on exists
    MESSAGE("glass grazing lum=" << lum_grazing << " face-on lum=" << lum_faceon);
    CHECK(lum_grazing > lum_faceon);                           // bright rim

    // (b) white Lambert sphere, NO light: its horizon face-on lum.
    const aleph::lowering::LoweredScene white_ls = make_material_scene(
        aleph::types::MaterialKind::Lambertian, Vec3{1, 1, 1}, 0.0f, 1.5f);
    const aleph::lowering::SwBuild w = aleph::lowering::build_sw_scene(white_ls, eye);
    f32 max_nv_w = -1.0f, lum_white_h = 0.0f;
    for (std::size_t i = 0; i < w.scene.faces.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 vk = w.scene.faces[i].verts[k];
            const Vec3 N = aleph::math::normalize(vk);
            const Vec3 ev = eye - vk;
            const Vec3 V = ev * (1.0f / aleph::math::length(ev));
            const f32 nv = aleph::math::dot(N, V);
            if (nv <= 0.0f) continue;
            if (std::fabs(N.y) < 0.1f && nv > max_nv_w) { max_nv_w = nv; lum_white_h = lum(w.scene.faces[i].vcol[k]); }
        }
    REQUIRE(max_nv_w > 0.0f);
    MESSAGE("glass center (horizon face-on) lum=" << lum_faceon_h
            << " white-Lambert horizon lum=" << lum_white_h);
    CHECK(lum_faceon_h < lum_white_h);                         // dim glass center
}

// 5. Determinism: same_face across two builds of a Metal+Dielectric+Lambert
// scene (with an eye). (The existing φ test already covers the φ-skip path.)
TEST_CASE("build_sw: material-kind scene is deterministic (same_face across builds)") {
    aleph::lowering::LoweredScene ls;
    ls.camera.look_from = Vec3{0, 0, 5};
    LoweredEntity metal;
    metal.source = NodeId{1};
    metal.world_geometry = SphereLocal{Vec3{-2, 0, 0}, 1.0f};
    metal.material.kind   = aleph::types::MaterialKind::Metal;
    metal.material.albedo = Vec3{0.85f, 0.85f, 0.9f};
    metal.material.fuzz   = 0.05f;
    ls.entities.push_back(metal);
    LoweredEntity glass;
    glass.source = NodeId{2};
    glass.world_geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    glass.material.kind = aleph::types::MaterialKind::Dielectric;
    glass.material.ior  = 1.5f;
    ls.entities.push_back(glass);
    LoweredEntity matte;
    matte.source = NodeId{3};
    matte.world_geometry = SphereLocal{Vec3{2, 0, 0}, 1.0f};
    matte.material.albedo = Vec3{0.2f, 0.6f, 0.3f};
    ls.entities.push_back(matte);
    LoweredEntity light;
    light.source = NodeId{100};
    light.world_geometry = QuadLocal{Vec3{-0.5f, 4, -0.5f}, Vec3{1, 0, 0}, Vec3{0, 0, 1}};
    light.material.emit = Vec3{3, 3, 3};
    ls.lights.push_back(light);

    const Vec3 eye{0, 0, 5};
    const aleph::lowering::SwBuild a = aleph::lowering::build_sw_scene(ls, eye);
    const aleph::lowering::SwBuild b = aleph::lowering::build_sw_scene(ls, eye);
    REQUIRE(a.scene.faces.size() == b.scene.faces.size());
    bool all_equal = true;
    for (std::size_t i = 0; i < a.scene.faces.size(); ++i)
        if (!same_face(a.scene.faces[i], b.scene.faces[i])) { all_equal = false; break; }
    CHECK(all_equal);
}
