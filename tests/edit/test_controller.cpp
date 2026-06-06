#include "doctest.h"

#include <algorithm>  // std::max (orbit-threads-eye vcol delta)
#include <cmath>      // std::abs
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>   // std::move (Graph / LoweredScene are move-only)
#include <variant>
#include <vector>

import aleph.math;       // Vec3, Mat4
import aleph.types;      // NodeId, NodeKind, geometry / material / node payloads
import aleph.graph;      // Graph, validate_all
import aleph.render.sw;  // SceneRT, Face
import aleph.lowering;   // lower, build_render_scene, Op, apply_op, LoweredScene,
                         // MaterialParams, OpError
import aleph.edit;       // EditorController (the headless core under test)

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise LoweredScene byte image

// Phase 6, SPEC §5 tests 2,3,4(controller) — the headless `EditorController`.
//
//   gesture ─▶ Op ─▶ apply_op ─▶ Graph ─▶ lower_incremental ─▶ {Scene, SceneRT}
//
// The controller (SPEC §3.2) owns the typed scene graph (the single source of
// truth), lowers it, builds BOTH backends (raster SceneRT + path-trace Scene),
// and turns editor gestures into Ops. It is HEADLESS: no SDL / aleph.window /
// aleph.editor — it does its OWN ray-vs-SceneRT-face pick against
// `build_sw_scene`'s `face_source` and carries its OWN small orbit camera.
//
// This file pins three SPEC §5 oracles:
//
//   (2) pick_maps_to_node — aim a pixel ray at a known entity -> `pick()` returns
//       that entity's source NodeId; aim a pixel at the background -> nullopt.
//
//   (3) apply_relower_consistency — after `controller.apply(Op)` the controller's
//       lowered IR is BYTE-IDENTICAL to a fresh `lower(graph)` on an independent
//       graph carrying the SAME committed mutation (frozen via lowering_freeze.hpp).
//       Checked for SetMaterial, AddObject and DeleteObject; the committed graph
//       satisfies `validate_all`; and the raster `face_source` has no dangling
//       faces (every source is a live entity; counts match).
//
//   (4) headless_script (controller part) — run a fixed Op SCRIPT (add 2 objects,
//       recolor 1, delete 1) through the controller and assert the final entity /
//       light counts AND that the controller's lowered state after EACH step is
//       byte-identical to a full re-lower of the parallel oracle graph.
//
// ── Determinism / move-only / parallel-oracle technique ─────────────────────
// `Graph` and `LoweredScene` are move-only, and the controller takes OWNERSHIP of
// its graph. To diff "controller vs fresh lower" we keep an INDEPENDENT oracle
// `Graph` built by the SAME factory and step it through the SAME `apply_op`
// sequence: node ids are minted off an allocator that starts at 0 (IdAllocator),
// so two factory builds produce byte-identical ids, and `apply_op` /
// `lower_incremental` are deterministic — hence `lower(oracle)` is exactly what
// the controller's incremental lowering must reproduce byte-for-byte (SPEC §2:
// incremental is an optimization, never a semantic divergence).
//
// No exceptions (aleph_flags_isa): every fallible path is `std::expected`; we
// REQUIRE has_value() before trusting a post-state.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Mat4;
using aleph::math::Vec3;

namespace {

using aleph_test_freeze::freeze;  // whole-LoweredScene padding-proof byte image

// ── canonical pickable scene ────────────────────────────────────────────────
// A root Transform (identity) Contains a Camera and ONE quad Mesh that faces the
// camera in the z=0 plane, centered at the orbit target (the origin). The quad
// spans [-1,1]^2 in x/y so the central view ray (NDC 0,0 -> straight at the
// target) lands squarely on it. Lambertian (non-emissive): exactly one drawable
// entity, no light-table member -> an unambiguous pick target.
struct OneQuad {
    Graph  g;
    NodeId root{}, cam{}, quad{}, quad_mat{};
};

OneQuad make_one_quad() {
    OneQuad s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // A quad in the z=0 plane spanning x,y in [-1,1], centered at the origin and
    // facing +z (toward the camera at z=+5): q=(-1,-1,0), u=(2,0,0), v=(0,2,0) ->
    // corners (-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0).
    s.quad = g.alloc_node_id();
    Mesh quad_mesh{s.quad, std::string("quad"), 0};
    quad_mesh.geometry = QuadLocal{Vec3{-1, -1, 0}, Vec3{2, 0, 0}, Vec3{0, 2, 0}};
    g.insert_node(std::move(quad_mesh));

    s.quad_mat = g.alloc_node_id();
    Material qmat{s.quad_mat, MaterialKind::Lambertian};
    qmat.albedo = Vec3{0.2f, 0.6f, 0.9f};
    qmat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(qmat));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.quad);
    (void)g.add_edge(EdgeKind::References, s.quad, s.quad_mat);
    return s;
}

// ── two-mesh scene for the apply/relower oracle + the headless script ────────
// root Transform (identity) Contains a Camera + two quad Meshes (each -> its own
// Lambertian). Two entities give the relower oracle teeth (a bystander whose bytes
// must not move) and a deletable target for the script. Quads sit in the z=0 plane
// (left at x≈-2, right at x≈+2) so both face the camera.
struct TwoMesh {
    Graph  g;
    NodeId root{}, cam{}, mesh_a{}, mat_a{}, mesh_b{}, mat_b{};
};

TwoMesh make_two_mesh() {
    TwoMesh s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    s.mesh_a = g.alloc_node_id();
    Mesh mesh_a{s.mesh_a, std::string("quad_a"), 0};
    mesh_a.geometry = QuadLocal{Vec3{-3, -1, 0}, Vec3{2, 0, 0}, Vec3{0, 2, 0}};
    g.insert_node(std::move(mesh_a));

    s.mat_a = g.alloc_node_id();
    Material mat_a{s.mat_a, MaterialKind::Lambertian};
    mat_a.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat_a.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_a));

    s.mesh_b = g.alloc_node_id();
    Mesh mesh_b{s.mesh_b, std::string("quad_b"), 0};
    mesh_b.geometry = QuadLocal{Vec3{1, -1, 0}, Vec3{2, 0, 0}, Vec3{0, 2, 0}};
    g.insert_node(std::move(mesh_b));

    s.mat_b = g.alloc_node_id();
    Material mat_b{s.mat_b, MaterialKind::Lambertian};
    mat_b.albedo = Vec3{0.0f, 1.0f, 0.0f};
    mat_b.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_b));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_a);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_b);
    (void)g.add_edge(EdgeKind::References, s.mesh_a, s.mat_a);
    (void)g.add_edge(EdgeKind::References, s.mesh_b, s.mat_b);
    return s;
}

// A green-Lambertian MaterialParams (the recolor target for the relower cases).
aleph::lowering::MaterialParams green_lambertian() {
    aleph::lowering::MaterialParams m{};
    m.kind   = MaterialKind::Lambertian;
    m.albedo = Vec3{0.0f, 1.0f, 0.0f};
    m.fuzz   = 0.0f;
    m.ior    = 1.5f;
    m.emit   = Vec3{0, 0, 0};
    return m;
}

// The controller's lowered IR, frozen to a flat byte image (padding-proof),
// versus a full `lower(oracle)` of the parallel graph — these MUST be byte-equal
// (SPEC §2/§5.3). Helper keeps the relower cases below uniform.
[[nodiscard]] bool lowered_matches_full(const aleph::edit::EditorController& c,
                                        const Graph& oracle) {
    auto full = aleph::lowering::lower(oracle);
    if (!full.has_value()) return false;
    return freeze(c.lowered()) == freeze(*full);
}

// ── Metal + Lambertian scene (view-tracking oracle) ──────────────────────────
// root Transform (identity) Contains a Camera + a Metal sphere (view-DEPENDENT
// vcol: chrome reflection tracks the eye) + a Lambertian sphere (view-INDEPENDENT
// vcol). The two materials give the orbit-threads-eye oracle teeth: orbiting must
// move the Metal vcol but leave the Lambertian vcol byte-identical.
struct MetalLambert {
    Graph  g;
    NodeId root{}, cam{}, metal_mesh{}, metal_mat{}, lamb_mesh{}, lamb_mat{};
};

MetalLambert make_metal_lambert() {
    MetalLambert s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{Mat4::identity()}});

    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // Metal sphere (left).
    s.metal_mesh = g.alloc_node_id();
    Mesh metal_mesh{s.metal_mesh, std::string("metal_sphere"), 0};
    metal_mesh.geometry = SphereLocal{Vec3{-2, 0, 0}, 1.0f};
    g.insert_node(std::move(metal_mesh));

    s.metal_mat = g.alloc_node_id();
    Material mmat{s.metal_mat, MaterialKind::Metal};
    mmat.albedo = Vec3{0.9f, 0.9f, 0.9f};
    mmat.fuzz   = 0.0f;
    g.insert_node(std::move(mmat));

    // Lambertian sphere (right).
    s.lamb_mesh = g.alloc_node_id();
    Mesh lamb_mesh{s.lamb_mesh, std::string("lamb_sphere"), 0};
    lamb_mesh.geometry = SphereLocal{Vec3{2, 0, 0}, 1.0f};
    g.insert_node(std::move(lamb_mesh));

    s.lamb_mat = g.alloc_node_id();
    Material lmat{s.lamb_mat, MaterialKind::Lambertian};
    lmat.albedo = Vec3{0.2f, 0.6f, 0.9f};
    g.insert_node(std::move(lmat));

    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.metal_mesh);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.lamb_mesh);
    (void)g.add_edge(EdgeKind::References, s.metal_mesh, s.metal_mat);
    (void)g.add_edge(EdgeKind::References, s.lamb_mesh,  s.lamb_mat);
    return s;
}

// Byte-equal comparison of two SceneRT faces' geometry/uv/vcol payload. Mirrors
// test_build_sw.cpp's `same_face` (the determinism/identity oracle).
[[nodiscard]] bool same_face(const aleph::render::sw::Face& a,
                             const aleph::render::sw::Face& b) {
    for (std::size_t k = 0; k < 4; ++k) {
        if (!(a.verts[k] == b.verts[k])) return false;
        if (!(a.uvs[k]   == b.uvs[k]))   return false;
        if (!(a.vcol[k]  == b.vcol[k]))  return false;
    }
    return true;
}

// The MaterialKind of the entity that sourced raster face `i` (via `face_source`
// -> the lowered IR's handle_map -> the entity's material).
[[nodiscard]] MaterialKind face_material_kind(const aleph::edit::EditorController& c,
                                              std::size_t face_i) {
    const NodeId src = c.face_source()[face_i];
    const std::uint32_t* idx = c.lowered().handle_map.get(src);
    REQUIRE(idx != nullptr);
    return c.lowered().entities[*idx].material.kind;
}

// Every raster face's source is a LIVE entity in the lowered IR (no dangling
// faces, SPEC §5.3): each `face_source` NodeId must be present in `handle_map`,
// and the two parallel vectors must be the same length.
[[nodiscard]] bool no_dangling_faces(const aleph::edit::EditorController& c) {
    const aleph::lowering::LoweredScene& ls = c.lowered();
    const aleph::render::sw::SceneRT&    sr = c.raster_scene();
    if (sr.faces.size() != c.face_source().size()) return false;
    for (NodeId src : c.face_source()) {
        if (ls.handle_map.get(src) == nullptr) return false;
    }
    return true;
}

}  // namespace

// ── SPEC §5.2 — pick_maps_to_node ───────────────────────────────────────────
// Build a controller over a known graph; aim its OWN orbit camera straight at a
// known entity. The CENTER pixel's ray is `fwd` (NDC 0,0) which, for an orbit
// camera looking at its target, points exactly at the target where the quad sits
// -> `pick()` resolves to the quad's NodeId. A pixel aimed off into empty space
// (the top-left corner, NDC ~(-1,+1) — well past the [-1,1] quad at a 40deg fov)
// hits nothing -> nullopt.
TEST_CASE("edit: pick resolves a center ray to the entity NodeId; background -> nullopt") {
    OneQuad s = make_one_quad();
    const NodeId quad_id = s.quad;

    aleph::edit::EditorController c{std::move(s.g)};

    // Aim the controller's own orbit camera at the origin (where the quad sits),
    // straight down -z from a +z eye: yaw=0, pitch=0 puts the eye on +z looking at
    // the target, matching the OneQuad camera intent.
    auto& cam = c.camera();
    cam.target = Vec3{0, 0, 0};
    cam.yaw    = 0.0f;
    cam.pitch  = 0.0f;
    cam.radius = 5.0f;

    REQUIRE(c.raster_scene().faces.size() > 0u);  // the quad rasterized (2 faces)

    // ── HIT: the dead-center pixel. ─────────────────────────────────────────
    const int cx = c.width() / 2;
    const int cy = c.height() / 2;
    std::optional<NodeId> hit = c.pick(cx, cy);
    REQUIRE(hit.has_value());
    CHECK(*hit == quad_id);

    // ── MISS: the top-left corner pixel aims into empty space. ──────────────
    std::optional<NodeId> miss = c.pick(0, 0);
    CHECK_FALSE(miss.has_value());
}

// ── SPEC §5.3 — apply_relower_consistency: SetMaterial ──────────────────────
// `controller.apply(SetMaterial)` recolors mesh_a; the controller's lowered IR
// must equal a fresh full `lower()` of an independent graph carrying the SAME
// committed SetMaterial. The committed graph stays invariant-valid and the raster
// face_source has no dangling faces.
TEST_CASE("edit: apply(SetMaterial) -> lowered == fresh lower(graph), byte-identical") {
    TwoMesh ctl = make_two_mesh();    // moved into the controller
    TwoMesh ora = make_two_mesh();    // independent parallel oracle (identical ids)
    const NodeId target = ctl.mesh_a;

    aleph::edit::EditorController c{std::move(ctl.g)};

    aleph::lowering::Op op = aleph::lowering::SetMaterial{target, green_lambertian()};

    // Apply through the controller (apply_op + lower_incremental + rebuild).
    auto applied = c.apply(op);
    REQUIRE(applied.has_value());

    // Replay the SAME mutation on the oracle graph and full-lower it.
    auto ora_applied = aleph::lowering::apply_op(ora.g, op);
    REQUIRE(ora_applied.has_value());
    REQUIRE(aleph::graph::validate_all(ora.g, static_cast<std::size_t>(-1)).has_value());

    // Byte-identical lowered IR (SPEC §2/§5.3) + no dangling raster faces.
    CHECK(lowered_matches_full(c, ora.g));
    CHECK(no_dangling_faces(c));

    // The recolor is observable in the controller's lowered IR.
    const std::uint32_t* idx = c.lowered().handle_map.get(target);
    REQUIRE(idx != nullptr);
    CHECK(c.lowered().entities[*idx].material.albedo == Vec3{0.0f, 1.0f, 0.0f});
}

// ── SPEC §5.3 — apply_relower_consistency: AddObject ────────────────────────
// Adding an object grows the lowered IR by exactly one entity; the controller's
// lowered IR must match a fresh full lower of the parallel graph with the SAME
// AddObject committed. Invariants hold; no dangling faces.
TEST_CASE("edit: apply(AddObject) -> lowered == fresh lower(graph), byte-identical") {
    TwoMesh ctl = make_two_mesh();
    TwoMesh ora = make_two_mesh();
    const NodeId parent = ctl.root;

    aleph::edit::EditorController c{std::move(ctl.g)};

    aleph::lowering::AddObject add{};
    add.parent   = parent;
    add.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    add.material = green_lambertian();

    const std::size_t before = c.lowered().entities.size();

    auto applied = c.apply(aleph::lowering::Op{add});
    REQUIRE(applied.has_value());

    aleph::lowering::AddObject add_ora{};
    add_ora.parent   = ora.root;
    add_ora.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    add_ora.material = green_lambertian();
    auto ora_applied = aleph::lowering::apply_op(ora.g, aleph::lowering::Op{add_ora});
    REQUIRE(ora_applied.has_value());
    REQUIRE(aleph::graph::validate_all(ora.g, static_cast<std::size_t>(-1)).has_value());

    CHECK(c.lowered().entities.size() == before + 1);
    CHECK(lowered_matches_full(c, ora.g));
    CHECK(no_dangling_faces(c));
}

// ── SPEC §5.3 — apply_relower_consistency: DeleteObject ─────────────────────
// Deleting a mesh shrinks the lowered IR by one entity and removes every face it
// sourced; the controller's lowered IR must match a fresh full lower of the
// parallel graph with the SAME DeleteObject committed. Invariants hold; the
// deleted mesh sources NO surviving raster face.
TEST_CASE("edit: apply(DeleteObject) -> lowered == fresh lower(graph), byte-identical") {
    TwoMesh ctl = make_two_mesh();
    TwoMesh ora = make_two_mesh();
    const NodeId victim = ctl.mesh_a;

    aleph::edit::EditorController c{std::move(ctl.g)};

    aleph::lowering::Op op = aleph::lowering::DeleteObject{victim};

    const std::size_t before = c.lowered().entities.size();

    auto applied = c.apply(op);
    REQUIRE(applied.has_value());

    auto ora_applied = aleph::lowering::apply_op(ora.g, op);
    REQUIRE(ora_applied.has_value());
    REQUIRE(aleph::graph::validate_all(ora.g, static_cast<std::size_t>(-1)).has_value());

    CHECK(c.lowered().entities.size() == before - 1);
    CHECK(lowered_matches_full(c, ora.g));
    CHECK(no_dangling_faces(c));

    // The victim is gone from the IR and sources no surviving face.
    CHECK(c.lowered().handle_map.get(victim) == nullptr);
    bool victim_face = false;
    for (NodeId src : c.face_source()) {
        if (src == victim) { victim_face = true; break; }
    }
    CHECK_FALSE(victim_face);
}

// ── SPEC §5.4 — headless_script (controller part) ───────────────────────────
// Run a fixed Op SCRIPT through the controller — add 2 objects, recolor 1, delete
// 1 — and assert (a) the final entity / light counts and (b) that after EVERY
// step the controller's lowered state is byte-identical to a full re-lower of the
// parallel oracle graph stepped through the same script. This proves the loop
// (apply_op -> lower_incremental -> rebuild) reproduces full lower at each step.
TEST_CASE("edit: scripted Op sequence -> final counts + per-step lowered == full re-lower") {
    TwoMesh ctl = make_two_mesh();   // starts with 2 entities, 0 lights
    TwoMesh ora = make_two_mesh();

    aleph::edit::EditorController c{std::move(ctl.g)};
    Graph& og = ora.g;

    REQUIRE(c.lowered().entities.size() == 2);
    REQUIRE(c.lowered().lights.empty());

    // Apply one Op to BOTH the controller and the oracle, then assert the
    // controller's lowered IR == a fresh full lower of the oracle (byte-identical),
    // with no dangling raster faces, after the step.
    auto step = [&](const aleph::lowering::Op& op) {
        auto a = c.apply(op);
        REQUIRE(a.has_value());
        auto b = aleph::lowering::apply_op(og, op);
        REQUIRE(b.has_value());
        REQUIRE(aleph::graph::validate_all(og, static_cast<std::size_t>(-1)).has_value());
        CHECK(lowered_matches_full(c, og));
        CHECK(no_dangling_faces(c));
    };

    // ── step 1: add object #1 (a sphere, non-emissive). ──────────────────────
    {
        aleph::lowering::AddObject add{};
        add.parent   = ora.root;  // same root id in both graphs (deterministic alloc)
        add.geometry = SphereLocal{Vec3{0, 2, 0}, 0.5f};
        add.material = green_lambertian();
        step(aleph::lowering::Op{add});
    }
    CHECK(c.lowered().entities.size() == 3);

    // ── step 2: add object #2 (another sphere). ──────────────────────────────
    {
        aleph::lowering::AddObject add{};
        add.parent   = ora.root;
        add.geometry = SphereLocal{Vec3{0, -2, 0}, 0.5f};
        add.material = green_lambertian();
        step(aleph::lowering::Op{add});
    }
    CHECK(c.lowered().entities.size() == 4);

    // ── step 3: recolor one ORIGINAL mesh (mesh_a; same id in both graphs). ──
    {
        aleph::lowering::MaterialParams blue{};
        blue.kind   = MaterialKind::Lambertian;
        blue.albedo = Vec3{0.0f, 0.0f, 1.0f};
        step(aleph::lowering::Op{aleph::lowering::SetMaterial{ora.mesh_a, blue}});
    }
    CHECK(c.lowered().entities.size() == 4);  // recolor changes no count

    // ── step 4: delete one ORIGINAL mesh (mesh_b). ───────────────────────────
    {
        step(aleph::lowering::Op{aleph::lowering::DeleteObject{ora.mesh_b}});
    }

    // ── final counts (SPEC §5.4): 2 originals + 2 added - 1 deleted = 3; the
    // recolored mesh stayed Lambertian (non-emissive), so the light table is
    // still empty. ──────────────────────────────────────────────────────────
    CHECK(c.lowered().entities.size() == 3);
    CHECK(c.lowered().lights.empty());

    // The recolored survivor carries its new albedo; the deleted mesh is gone.
    const std::uint32_t* a_idx = c.lowered().handle_map.get(ora.mesh_a);
    REQUIRE(a_idx != nullptr);
    CHECK(c.lowered().entities[*a_idx].material.albedo == Vec3{0.0f, 0.0f, 1.0f});
    CHECK(c.lowered().handle_map.get(ora.mesh_b) == nullptr);
}

// ── live-orbit slice 4c-i-b — rebake_view_sw == the full sw_ (sim-OFF) ───────
// `rebake_view_sw()` is exactly the sw_ half of `rebuild_backends_from_prev`, so
// (sim OFF) the controller's `raster_scene()` faces must be `same_face`-byte-
// identical to a fresh `build_sw_scene(c.lowered(), c.camera().look_from())` at
// the same eye. (Thin wiring check — the per-pixel shading is pinned in
// test_build_sw.cpp.)
TEST_CASE("edit: rebake_view_sw reproduces the full sw_ bake (sim-OFF, byte-identical)") {
    TwoMesh ctl = make_two_mesh();
    aleph::edit::EditorController c{std::move(ctl.g)};

    // Move off the default pose so the eye-explicit overload exercises a real
    // (non-trivial) look_from.
    c.camera().orbit(120.0f, 40.0f);
    c.rebake_view_sw();

    const aleph::lowering::SwBuild ref =
        aleph::lowering::build_sw_scene(c.lowered(), c.camera().look_from());

    REQUIRE(c.raster_scene().faces.size() == ref.scene.faces.size());
    bool all_equal = true;
    for (std::size_t i = 0; i < ref.scene.faces.size(); ++i)
        if (!same_face(c.raster_scene().faces[i], ref.scene.faces[i])) { all_equal = false; break; }
    CHECK(all_equal);
}

// ── live-orbit slice 4c-i-b — rebake_view_sw leaves prim_source_ untouched ───
// render_/BVH + prim_source_ are view-INDEPENDENT and rebuilt ONLY by the full
// path. A sw_-only re-bake (even after a large orbit) must leave `prim_source()`
// byte-unchanged (element-wise NodeId ==).
TEST_CASE("edit: rebake_view_sw leaves prim_source_ byte-unchanged") {
    MetalLambert ctl = make_metal_lambert();
    aleph::edit::EditorController c{std::move(ctl.g)};

    const std::vector<NodeId> before = c.prim_source();  // copy
    c.camera().orbit(400.0f, 0.0f);                       // large yaw
    c.rebake_view_sw();
    const std::vector<NodeId>& after = c.prim_source();

    REQUIRE(after.size() == before.size());
    bool unchanged = true;
    for (std::size_t i = 0; i < before.size(); ++i)
        if (!(after[i] == before[i])) { unchanged = false; break; }
    CHECK(unchanged);
}

// ── live-orbit slice 4c-i-b — has_view_dependent_material classifies ─────────
// Metal (or Dielectric) entity present -> true; an all-Lambertian scene -> false.
TEST_CASE("edit: has_view_dependent_material is true for Metal, false for all-Lambertian") {
    {
        MetalLambert ml = make_metal_lambert();
        aleph::edit::EditorController c{std::move(ml.g)};
        CHECK(c.has_view_dependent_material());           // has a Metal sphere
    }
    {
        // Dielectric also triggers view-dependence (the OR's second arm).
        Graph g;
        const NodeId root = g.alloc_node_id();
        g.insert_node(Transform{root, 0, LocalTransform{Mat4::identity()}});
        const NodeId cam = g.alloc_node_id();
        Camera c0{cam, std::string("sensor0")};
        c0.look_from = Vec3{0, 0, 5};
        g.insert_node(std::move(c0));
        const NodeId mesh = g.alloc_node_id();
        Mesh glass{mesh, std::string("glass"), 0};
        glass.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
        g.insert_node(std::move(glass));
        const NodeId mat = g.alloc_node_id();
        Material gmat{mat, MaterialKind::Dielectric};
        gmat.ior = 1.5f;
        g.insert_node(std::move(gmat));
        (void)g.add_edge(EdgeKind::Contains,   root, cam);
        (void)g.add_edge(EdgeKind::Contains,   root, mesh);
        (void)g.add_edge(EdgeKind::References, mesh, mat);
        aleph::edit::EditorController c{std::move(g)};
        CHECK(c.has_view_dependent_material());           // Dielectric
    }
    {
        TwoMesh tm = make_two_mesh();                     // both Lambertian
        aleph::edit::EditorController c{std::move(tm.g)};
        CHECK_FALSE(c.has_view_dependent_material());
    }
}

// ── live-orbit slice 4c-i-b — orbit threads the live eye (the new behaviour) ─
// A Metal sphere + a Lambertian sphere. Bake, record per-face vcol[0]; orbit by a
// LARGE yaw (400 ≈ 3.2 rad — yaw unclamped, kOrbitSpeed=0.008) so the reflection
// moves unambiguously; `rebake_view_sw()`. The Metal faces' vcol must MOVE
// (max |Δ| > 0.05 — the eye reached the bake) while EVERY Lambertian face vcol is
// byte-IDENTICAL (the default shade branch never reads V). Proves the threading +
// the gate rationale, not the shading math.
TEST_CASE("edit: orbit threads the eye -> Metal vcol moves, Lambertian vcol byte-identical") {
    MetalLambert ml = make_metal_lambert();
    aleph::edit::EditorController c{std::move(ml.g)};

    const std::size_t nfaces = c.raster_scene().faces.size();
    REQUIRE(nfaces > 0u);
    REQUIRE(c.face_source().size() == nfaces);

    // Snapshot vcol[0] per face, tagged by source material kind.
    std::vector<Vec3>         vcol_old(nfaces);
    std::vector<MaterialKind> kind(nfaces);
    std::size_t metal_faces = 0, lamb_faces = 0;
    for (std::size_t i = 0; i < nfaces; ++i) {
        vcol_old[i] = c.raster_scene().faces[i].vcol[0];
        kind[i]     = face_material_kind(c, i);
        if (kind[i] == MaterialKind::Metal)           ++metal_faces;
        else if (kind[i] == MaterialKind::Lambertian) ++lamb_faces;
    }
    REQUIRE(metal_faces > 0u);
    REQUIRE(lamb_faces  > 0u);

    c.camera().orbit(400.0f, 0.0f);
    c.rebake_view_sw();
    REQUIRE(c.raster_scene().faces.size() == nfaces);  // geometry count stable

    aleph::math::f32 max_metal_delta = 0.0f;
    bool lambertian_identical = true;
    for (std::size_t i = 0; i < nfaces; ++i) {
        const Vec3 v = c.raster_scene().faces[i].vcol[0];
        if (kind[i] == MaterialKind::Metal) {
            const Vec3 d = v - vcol_old[i];
            max_metal_delta = std::max(max_metal_delta,
                std::max(std::abs(d.x), std::max(std::abs(d.y), std::abs(d.z))));
        } else if (kind[i] == MaterialKind::Lambertian) {
            if (!(v == vcol_old[i])) lambertian_identical = false;
        }
    }
    INFO("max Metal vcol delta = " << max_metal_delta);
    CHECK(max_metal_delta > 0.05f);       // chrome reflection tracked the eye
    CHECK(lambertian_identical);          // view-independent: byte-unchanged
}
