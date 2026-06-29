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
import aleph.dpo;       // RewriteRecord (apply_op's structured success report)
import aleph.lowering;

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise byte serializers

// SPEC §8.6 — add_object / add_light (the return path, STRUCTURAL ops).
//
//   Editing is a MORPHISM, not a mutation of the render product (SPEC §1): an
//   editor gesture becomes an `Op` that mutates the GRAPH (the single source of
//   truth); the renderable IR is then re-derived by re-lowering. `AddObject` is
//   in v1 so the edit loop is SYMMETRIC — the editor can add geometry, not only
//   lights (SPEC §5). The structural family is transactional (all-or-nothing via
//   `aleph.dpo`): an op returns a new VALID state or fails with NO partial effect.
//
// Gestures under test:
//   * apply_op(AddObject) — create a Mesh + Material + the References/Contains
//                           edges that wire it into the scene (SPEC §5). After a
//                           re-lower the ENTITY count grows by one.
//   * apply_op(AddLight)  — create an explicit Light node + its Contains edge.
//                           After a re-lower the LIGHT count grows by one.
//
// Oracle (SPEC §8.6): re-lowering after each op yields entity/light counts that
// grew exactly as expected, and EVERY surviving handle is stable — its bytes do
// not move across the re-lower. To make "stable" literal we freeze each entity /
// light / camera / handle_map entry into a flat byte image (POD fields walked in
// IR iteration order, field-wise so alignas(16) padding never leaks in) and
// compare images — the same padding-proof technique test_edit_material uses,
// which also pins insertion order and f32 bit-patterns.
//
// No exceptions (aleph_flags_isa): `apply_op` returns std::expected; we REQUIRE
// has_value() before trusting the post-state. The op reports an
// `aleph::dpo::RewriteRecord` whose `created_nodes` name the freshly minted ids,
// so the test can identify the NEW entity/light without guessing.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical seed graph ─────────────────────────────────────────────────────
// root Transform (identity) Contains: a Camera, one Mesh (-> red Lambertian
// material, NOT emissive), and one explicit Light. Two pre-existing survivors
// (the mesh entity and the light) give the "handles stable" oracle teeth: adding
// a third thing must leave both of them byte-identical.
struct Seed {
    Graph  g;
    NodeId root{}, cam{}, mesh{}, mat{}, light{};
};

Seed make_seed() {
    Seed s;
    Graph& g = s.g;

    // root Transform at identity (no incoming Contains -> a root). New objects
    // are wired under this transform so they inherit world == identity.
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    // Camera with a concrete pose (must survive every add verbatim).
    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // The lone seed Mesh — a unit sphere at the origin.
    s.mesh = g.alloc_node_id();
    Mesh mesh{s.mesh, std::string("sphere0"), 0};
    mesh.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh));

    // Its red Lambertian material — NOT emissive, so it stays OUT of the light
    // table (the seed light table is exactly the explicit Light node below).
    s.mat = g.alloc_node_id();
    Material mat{s.mat, MaterialKind::Lambertian};
    mat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat));

    // An explicit Light node — the lone seed light-table member.
    s.light = g.alloc_node_id();
    Light light{s.light, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 2, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    // Hierarchy: root contains camera, mesh and light.
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    // The mesh references its material (must resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh, s.mat);
    return s;
}

// ── padding-proof byte-image serializers for the frozen IR ───────────────────
// Provided by lowering_freeze.hpp. We freeze entities / lights / camera /
// handle_map into flat byte images and compare images. We walk LEAVES explicitly
// (never memcpy whole structs): the IR value types embed `aleph::math::Vec3`
// (alignas(16)), so SphereLocal / MaterialParams / LoweredCamera carry inter-
// field + trailing PADDING that aggregate init does NOT zero. A raw memcpy would
// compare indeterminate padding bytes; leaf-wise compares the semantic state.
using aleph_test_freeze::freeze_camera;
using aleph_test_freeze::freeze_entity;
using aleph_test_freeze::freeze_handle_map;

// Look up the entities index of `source` via the stable handle_map.
const aleph::lowering::LoweredEntity*
entity_for(const aleph::lowering::LoweredScene& ls, NodeId source) {
    const std::uint32_t* idx = ls.handle_map.get(source);
    if (idx == nullptr) return nullptr;
    return &ls.entities[*idx];
}

// Find a light-table entry by its source NodeId (lights have no handle_map slot —
// the handle_map keys only the renderable `entities`, SPEC §4.1).
const aleph::lowering::LoweredEntity*
light_for(const aleph::lowering::LoweredScene& ls, NodeId source) {
    for (const auto& l : ls.lights) {
        if (l.source == source) return &l;
    }
    return nullptr;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AddObject: create a Mesh + Material + References/Contains edges -> re-lower ->
// the entity count grows by exactly one; the new entity carries the requested
// geometry + material; both seed survivors (the seed mesh entity and the light)
// are byte-identical across the re-lower.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: AddObject grows entity count by one; survivors stable") {
    Seed s = make_seed();

    // ── lower BEFORE the op ──────────────────────────────────────────────────
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& a = *before;

    // Pin the pre-state: 1 entity (the seed mesh), 1 light (the explicit Light).
    REQUIRE(a.entities.size() == 1);
    REQUIRE(a.lights.size() == 1);
    const aleph::lowering::LoweredEntity* a_mesh  = entity_for(a, s.mesh);
    const aleph::lowering::LoweredEntity* a_light = light_for(a, s.light);
    REQUIRE(a_mesh  != nullptr);
    REQUIRE(a_light != nullptr);

    // Freeze the survivors + the camera + the handle_map from the pre-state.
    const std::vector<std::byte> a_mesh_image   = freeze_entity(*a_mesh);
    const std::vector<std::byte> a_light_image  = freeze_entity(*a_light);
    const std::vector<std::byte> a_camera       = freeze_camera(a);

    // ── the op: AddObject under the root transform ───────────────────────────
    // A structural op (SPEC §5): it mints a Mesh node + a Material node and wires
    // the Mesh —References-> Material and parent —Contains-> Mesh edges in ONE
    // transaction. We hand it the parent to attach under, the LOCAL geometry, and
    // the new material as `MaterialParams` (the same frozen, renderer-independent
    // material view the IR uses, SPEC §4.1) — so the editor speaks one material
    // vocabulary on both the down (lower) and up (op) paths.
    aleph::lowering::MaterialParams new_mat{};
    new_mat.kind   = MaterialKind::Metal;
    new_mat.albedo = Vec3{0.2f, 0.4f, 0.9f};
    new_mat.fuzz   = 0.15f;
    new_mat.ior    = 1.5f;
    new_mat.emit   = Vec3{0, 0, 0};  // NOT emissive -> must NOT join the light table.
    new_mat.uv_scale = 9.0f;

    aleph::lowering::AddObject add{};
    add.parent   = s.root;
    add.geometry = SphereLocal{Vec3{3, 0, 0}, 0.5f};
    add.material = new_mat;

    aleph::lowering::Op op = add;
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // The transaction reports the freshly minted ids. AddObject creates a Mesh
    // node and a Material node, so created_nodes is non-empty; we use it to find
    // the NEW mesh below (it is the created node that lowers to an entity).
    const aleph::dpo::RewriteRecord& rec = *applied;
    REQUIRE_FALSE(rec.created_nodes.empty());

    // ── re-lower AFTER the op ────────────────────────────────────────────────
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const aleph::lowering::LoweredScene& b = *after;

    // (1) entity count grew by EXACTLY one; the light count is UNCHANGED (the new
    //     object is not emissive, so it does not join the light table, SPEC §3).
    CHECK(b.entities.size() == a.entities.size() + 1);
    CHECK(b.lights.size()   == a.lights.size());

    // (2) the seed survivors are byte-identical across the re-lower: the seed
    //     mesh entity (located via the STABLE handle_map), the light, the camera.
    const aleph::lowering::LoweredEntity* b_mesh  = entity_for(b, s.mesh);
    const aleph::lowering::LoweredEntity* b_light = light_for(b, s.light);
    REQUIRE(b_mesh  != nullptr);
    REQUIRE(b_light != nullptr);
    CHECK(freeze_entity(*b_mesh)  == a_mesh_image);
    CHECK(freeze_entity(*b_light) == a_light_image);
    CHECK(freeze_camera(b)        == a_camera);

    // (3) the surviving handle_map slot is stable: the seed mesh still maps to a
    //     valid entities index pointing back at itself.
    const std::uint32_t* seed_idx = b.handle_map.get(s.mesh);
    REQUIRE(seed_idx != nullptr);
    CHECK(b.entities[*seed_idx].source == s.mesh);

    // (4) the NEW entity exists, is keyed in the handle_map, and carries exactly
    //     the requested world geometry + material. We identify it as the lowered
    //     entity whose source is NOT in the pre-state handle_map.
    const aleph::lowering::LoweredEntity* added = nullptr;
    for (const auto& e : b.entities) {
        if (!a.handle_map.contains(e.source)) {
            REQUIRE(added == nullptr);  // exactly one genuinely new entity
            added = &e;
        }
    }
    REQUIRE(added != nullptr);
    CHECK(b.handle_map.contains(added->source));
    CHECK(added->material.kind == MaterialKind::Metal);
    CHECK(added->material.albedo == Vec3{0.2f, 0.4f, 0.9f});
    CHECK(added->material.uv_scale == doctest::Approx(9.0f));
    REQUIRE(std::holds_alternative<SphereLocal>(added->world_geometry));
    // Wired under the identity root -> world geometry == the local geometry.
    CHECK(std::get<SphereLocal>(added->world_geometry).center == Vec3{3, 0, 0});
    CHECK(std::get<SphereLocal>(added->world_geometry).radius == doctest::Approx(0.5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// AddLight: create an explicit Light node (+ its Contains edge) -> re-lower ->
// the light count grows by exactly one; the entity count is UNCHANGED; all seed
// survivors are byte-identical across the re-lower.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: AddLight grows light count by one; survivors stable") {
    Seed s = make_seed();

    // ── lower BEFORE the op ──────────────────────────────────────────────────
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& a = *before;

    REQUIRE(a.entities.size() == 1);
    REQUIRE(a.lights.size() == 1);
    const aleph::lowering::LoweredEntity* a_mesh      = entity_for(a, s.mesh);
    const aleph::lowering::LoweredEntity* a_seedlight = light_for(a, s.light);
    REQUIRE(a_mesh      != nullptr);
    REQUIRE(a_seedlight != nullptr);

    const std::vector<std::byte> a_mesh_image      = freeze_entity(*a_mesh);
    const std::vector<std::byte> a_seedlight_image = freeze_entity(*a_seedlight);
    const std::vector<std::byte> a_camera          = freeze_camera(a);
    const std::vector<std::byte> a_handle_map      = freeze_handle_map(a);

    // ── the op: AddLight under the root transform ────────────────────────────
    // A structural op (SPEC §5): mint a Light node and wire parent —Contains->
    // Light in one transaction. A Light is an EXPLICIT sampling source in its own
    // right (SPEC §3): it carries emission + a geometry payload, and joins the
    // light table directly — NOT via an emissive material.
    aleph::lowering::AddLight add{};
    add.parent   = s.root;
    add.kind     = LightKind::Area;
    add.emission = Vec3{2.0f, 2.0f, 2.0f};
    add.geometry = QuadLocal{Vec3{1, 3, 1}, Vec3{1, 0, 0}, Vec3{0, 0, 1}};

    aleph::lowering::Op op = add;
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    const aleph::dpo::RewriteRecord& rec = *applied;
    REQUIRE_FALSE(rec.created_nodes.empty());

    // ── re-lower AFTER the op ────────────────────────────────────────────────
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const aleph::lowering::LoweredScene& b = *after;

    // (1) light count grew by EXACTLY one; the entity count is UNCHANGED (adding
    //     a Light mints no renderable entity, SPEC §3 — lights are their own table).
    CHECK(b.lights.size()   == a.lights.size() + 1);
    CHECK(b.entities.size() == a.entities.size());

    // (2) the seed survivors are byte-identical across the re-lower: the mesh
    //     entity, the seed light, the camera, AND the whole handle_map (adding a
    //     light mints no entity, so the entity-keyed handle_map must not move).
    const aleph::lowering::LoweredEntity* b_mesh      = entity_for(b, s.mesh);
    const aleph::lowering::LoweredEntity* b_seedlight = light_for(b, s.light);
    REQUIRE(b_mesh      != nullptr);
    REQUIRE(b_seedlight != nullptr);
    CHECK(freeze_entity(*b_mesh)      == a_mesh_image);
    CHECK(freeze_entity(*b_seedlight) == a_seedlight_image);
    CHECK(freeze_camera(b)            == a_camera);
    CHECK(freeze_handle_map(b)        == a_handle_map);

    // (3) the NEW light exists and carries exactly the requested emission/geometry.
    //     It is the light-table entry whose source matched neither seed survivor.
    const aleph::lowering::LoweredEntity* added = nullptr;
    for (const auto& l : b.lights) {
        if (l.source != s.light && l.source != s.mesh) {
            REQUIRE(added == nullptr);  // exactly one genuinely new light
            added = &l;
        }
    }
    REQUIRE(added != nullptr);
    // A Light surfaces in the table as an emissive entry carrying its emission.
    CHECK(added->material.kind == MaterialKind::Emissive);
    CHECK(added->material.emit == Vec3{2.0f, 2.0f, 2.0f});
    REQUIRE(std::holds_alternative<QuadLocal>(added->world_geometry));
    // Wired under the identity root -> world geometry == the local geometry.
    CHECK(std::get<QuadLocal>(added->world_geometry).q == Vec3{1, 3, 1});
}

// ─────────────────────────────────────────────────────────────────────────────
// AddObject then AddLight, in sequence: counts grow monotonically and stay
// CONSISTENT (each op grows exactly its own table), and the original seed mesh
// remains byte-identical through BOTH re-lowers — the editor can compose adds.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: AddObject then AddLight compose; counts grow consistently") {
    Seed s = make_seed();

    auto l0 = aleph::lowering::lower(s.g);
    REQUIRE(l0.has_value());
    const std::size_t e0 = l0->entities.size();
    const std::size_t k0 = l0->lights.size();
    const aleph::lowering::LoweredEntity* seed0 = entity_for(*l0, s.mesh);
    REQUIRE(seed0 != nullptr);
    const std::vector<std::byte> seed_image = freeze_entity(*seed0);

    // AddObject -> entities += 1, lights unchanged.
    aleph::lowering::AddObject add_obj{};
    add_obj.parent   = s.root;
    add_obj.geometry = TriLocal{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    add_obj.material = aleph::lowering::MaterialParams{};  // default Lambertian, not emissive
    {
        aleph::lowering::Op op = add_obj;
        auto r = aleph::lowering::apply_op(s.g, op);
        REQUIRE(r.has_value());
    }
    auto l1 = aleph::lowering::lower(s.g);
    REQUIRE(l1.has_value());
    CHECK(l1->entities.size() == e0 + 1);
    CHECK(l1->lights.size()   == k0);

    // AddLight -> lights += 1, entities unchanged from the post-AddObject state.
    aleph::lowering::AddLight add_light{};
    add_light.parent   = s.root;
    add_light.kind     = LightKind::Point;
    add_light.emission = Vec3{5, 5, 5};
    add_light.geometry = SphereLocal{Vec3{0, 5, 0}, 0.1f};
    {
        aleph::lowering::Op op = add_light;
        auto r = aleph::lowering::apply_op(s.g, op);
        REQUIRE(r.has_value());
    }
    auto l2 = aleph::lowering::lower(s.g);
    REQUIRE(l2.has_value());
    CHECK(l2->entities.size() == e0 + 1);
    CHECK(l2->lights.size()   == k0 + 1);

    // The original seed mesh is still byte-identical after BOTH structural ops:
    // surviving handles are stable across composed edits (SPEC §8.6 / §9).
    const aleph::lowering::LoweredEntity* seed2 = entity_for(*l2, s.mesh);
    REQUIRE(seed2 != nullptr);
    CHECK(freeze_entity(*seed2) == seed_image);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddObject then DeleteObject ROUND-TRIPS to baseline: DeleteObject CASCADES the
// minted per-object Transform (now childless) and the minted Material (this mesh
// was its only referrer), so the graph returns EXACTLY to its pre-add node/edge
// counts — no leak — and a re-lower is byte-identical to the pre-add lowering.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: AddObject then DeleteObject round-trips graph + lowering to baseline") {
    Seed s = make_seed();

    const std::size_t nodes0 = s.g.node_count();
    const std::size_t edges0 = s.g.edge_count();
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const std::vector<std::byte> img_before = aleph_test_freeze::freeze(*before);

    // AddObject mints Transform + Mesh + Material and three wiring edges.
    aleph::lowering::AddObject add{};
    add.parent   = s.root;
    add.geometry = SphereLocal{Vec3{3, 0, 0}, 0.5f};
    add.material = aleph::lowering::MaterialParams{};  // default Lambertian
    aleph::lowering::Op op = add;
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    REQUIRE(s.g.node_count() == nodes0 + 3);
    REQUIRE(s.g.edge_count() == edges0 + 3);

    // Find the minted Mesh in created_nodes BY KIND (never by position).
    NodeId minted{};
    bool found = false;
    for (const NodeId id : applied->created_nodes) {
        const Node* n = s.g.node(id);
        if (n != nullptr && kind_of(*n) == NodeKind::Mesh) { minted = id; found = true; }
    }
    REQUIRE(found);

    // DeleteObject must reclaim the WHOLE AddObject footprint (mesh + childless
    // per-object Transform + exclusive Material), not just the mesh.
    aleph::lowering::Op del = aleph::lowering::DeleteObject{minted};
    auto deleted = aleph::lowering::apply_op(s.g, del);
    REQUIRE(deleted.has_value());

    // Counts return EXACTLY to baseline (no node/edge leak); invariants hold.
    CHECK(s.g.node_count() == nodes0);
    CHECK(s.g.edge_count() == edges0);
    CHECK(aleph::graph::validate_all(s.g, static_cast<std::size_t>(-1)).has_value());

    // Re-lowering is byte-identical to the pre-add lowering: the round trip is
    // observationally a no-op on the derived IR.
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    CHECK(aleph_test_freeze::freeze(*after) == img_before);
}

// AddObject must mint a PER-OBJECT Transform so the new mesh is independently
// posable: parent ─Contains→ Transform ─Contains→ Mesh (not parent→Mesh direct).
TEST_CASE("lowering: AddObject mints a per-object Transform parent for the mesh") {
    Seed s = make_seed();

    aleph::lowering::AddObject add{};
    add.parent   = s.root;
    add.geometry = SphereLocal{Vec3{3, 0, 0}, 0.5f};
    add.material = aleph::lowering::MaterialParams{};  // default Lambertian
    aleph::lowering::Op op = add;
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // The added Mesh is the created node of kind Mesh.
    const aleph::dpo::RewriteRecord& rec = *applied;
    NodeId new_mesh{};
    bool found_mesh = false;
    for (const NodeId id : rec.created_nodes) {
        const Node* n = s.g.node(id);
        if (n != nullptr && kind_of(*n) == NodeKind::Mesh) {
            new_mesh = id; found_mesh = true;
        }
    }
    REQUIRE(found_mesh);

    // Walk UP one Contains edge from the mesh: its parent must be a Transform...
    NodeId owning_xf{};
    bool mesh_has_xf_parent = false;
    for (auto [eid, e] : s.g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::Contains && e.dst == new_mesh) {
            const Node* src = s.g.node(e.src);
            REQUIRE(src != nullptr);
            CHECK(kind_of(*src) == NodeKind::Transform);
            owning_xf = e.src; mesh_has_xf_parent = true;
        }
    }
    REQUIRE(mesh_has_xf_parent);

    // ...and THAT Transform must be Contained by the original parent (root).
    bool xf_under_parent = false;
    for (auto [eid, e] : s.g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::Contains && e.src == s.root && e.dst == owning_xf)
            xf_under_parent = true;
    }
    CHECK(xf_under_parent);
}
