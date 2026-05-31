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

// SPEC §8.5 — edit_material (the return path, attribute op).
//
//   Editing is a MORPHISM, not a mutation (SPEC §1): an editor gesture becomes an
//   `Op` that mutates the GRAPH (the single source of truth); the renderable IR is
//   then re-derived by re-lowering. The renderer holds no semantic authority.
//
// Gesture under test: `apply_op(SetMaterial)` retargets the material on ONE mesh.
//   1. lower a minimal graph  -> LoweredScene A
//   2. apply_op(SetMaterial)  -> mutate the GRAPH (all-or-nothing; SPEC §5)
//   3. re-lower               -> LoweredScene B
//
// Oracle (SPEC §8.5): re-lowering yields a scene where ONLY the edited entity's
// `MaterialParams` changed; EVERY other handle is byte-identical. To make "byte-
// identical" literal we freeze each entity / light / the camera / the handle_map
// into a flat byte image (POD fields walked in IR iteration order) and compare the
// images — the same technique test_lower_minimal uses, which also pins insertion
// order and f32 bit-patterns.
//
// The fixture has TWO meshes so the oracle has teeth: a "target" mesh (whose
// material we edit) and a "bystander" mesh (whose entity bytes MUST NOT move).
// Without a second entity, "all other handles byte-identical" is vacuous.
//
// No exceptions (aleph_flags_isa): `apply_op` returns std::expected; we REQUIRE
// has_value() before trusting the post-state. A failed op must leave the graph
// untouched (all-or-nothing), but the happy path is what §8.5 pins.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical two-mesh enriched graph ───────────────────────────────────────
// root Transform (identity) Contains: a Camera, mesh_a (-> mat_a, red Lambertian),
// mesh_b (-> mat_b, metal grey), and an explicit Light. Two meshes => two entities
// in deterministic insertion order, so editing mat_a must leave entity[b] byte-
// identical. Neither material is emissive, so the light table is exactly the Light
// node and must also survive the edit unchanged.
struct TwoMesh {
    Graph  g;
    NodeId root{}, cam{}, mesh_a{}, mat_a{}, mesh_b{}, mat_b{}, light{};
};

TwoMesh make_two_mesh() {
    TwoMesh s;
    Graph& g = s.g;

    // root Transform at identity (no incoming Contains -> a root).
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    // Camera with a concrete pose (must survive the edit verbatim).
    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // mesh_a — the EDIT TARGET — a unit sphere at the origin.
    s.mesh_a = g.alloc_node_id();
    Mesh mesh_a{s.mesh_a, std::string("sphere_a"), 0};
    mesh_a.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_a));

    // mat_a — red Lambertian, NOT emissive -> stays out of the light table.
    s.mat_a = g.alloc_node_id();
    Material mat_a{s.mat_a, MaterialKind::Lambertian};
    mat_a.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat_a.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_a));

    // mesh_b — the BYSTANDER — a unit sphere at +2x.
    s.mesh_b = g.alloc_node_id();
    Mesh mesh_b{s.mesh_b, std::string("sphere_b"), 0};
    mesh_b.geometry = SphereLocal{Vec3{2, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_b));

    // mat_b — grey Metal with some fuzz, NOT emissive.
    s.mat_b = g.alloc_node_id();
    Material mat_b{s.mat_b, MaterialKind::Metal};
    mat_b.albedo = Vec3{0.5f, 0.5f, 0.5f};
    mat_b.fuzz   = 0.25f;
    mat_b.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_b));

    // explicit Light node — the lone light-table member.
    s.light = g.alloc_node_id();
    Light light{s.light, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 2, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    // Hierarchy.
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_a);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_b);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    // Each mesh references its own material (both must resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh_a, s.mat_a);
    (void)g.add_edge(EdgeKind::References, s.mesh_b, s.mat_b);
    return s;
}

// ── byte-image serializers for the frozen IR ────────────────────────────────
// We freeze each entity / light / camera / handle_map into a flat byte image and
// compare the images. The oracle is "byte-identical meaningful state across the
// re-lower"; to make that literal AND robust we walk FIELDS explicitly rather
// than memcpy'ing whole structs.
//
// Why field-wise: the IR's value types embed `aleph::math::Vec3`, which is
// `alignas(16)`. That makes `SphereLocal`/`MaterialParams`/`LoweredCamera` carry
// inter-field and trailing PADDING (e.g. SphereLocal is 32 bytes for a 16-byte
// payload). `lower()` builds these via aggregate init (`SphereLocal{world_pt,
// r}`, `MaterialParams{kind,...}`), which does NOT zero padding — so a raw memcpy
// would compare INDETERMINATE padding bytes that have nothing to do with the
// edit, and a re-lower onto freshly-allocated vectors can leave different garbage
// there. Walking fields compares exactly the semantic state and is padding-proof.

// Append the raw bytes of a scalar (integers / enums / f32). These have no
// internal padding, so a raw copy is exact and deterministic.
template <typename T>
void put(std::vector<std::byte>& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// A Vec3 by its three f32 components (skips the alignas(16) padding lane).
void put_vec3(std::vector<std::byte>& out, const Vec3& v) {
    put(out, v.x);
    put(out, v.y);
    put(out, v.z);
}

// A geometry primitive, field by field, after the variant tag.
void put_geometry(std::vector<std::byte>& out,
                  const aleph::types::GeometryPayload& g) {
    // Tag first so a Sphere/Quad/Tri can never collide on payload bytes alone.
    put(out, static_cast<std::uint32_t>(g.index()));
    std::visit(
        [&](const auto& prim) {
            using P = std::decay_t<decltype(prim)>;
            if constexpr (std::is_same_v<P, SphereLocal>) {
                put_vec3(out, prim.center);
                put(out, prim.radius);
            } else if constexpr (std::is_same_v<P, QuadLocal>) {
                put_vec3(out, prim.q);
                put_vec3(out, prim.u);
                put_vec3(out, prim.v);
            } else {  // TriLocal
                put_vec3(out, prim.a);
                put_vec3(out, prim.b);
                put_vec3(out, prim.c);
            }
        },
        g);
}

// MaterialParams, field by field (kind + the four physical params).
void put_material(std::vector<std::byte>& out,
                  const aleph::lowering::MaterialParams& m) {
    put(out, static_cast<std::uint32_t>(m.kind));
    put_vec3(out, m.albedo);
    put(out, m.fuzz);
    put(out, m.ior);
    put_vec3(out, m.emit);
}

// One entity (or light-table entry) -> flat bytes: source id, world geometry,
// then the MaterialParams bundle — all field-wise (padding-proof).
std::vector<std::byte> freeze_entity(const aleph::lowering::LoweredEntity& e) {
    std::vector<std::byte> out;
    put(out, e.source.value);
    put_geometry(out, e.world_geometry);
    put_material(out, e.material);
    return out;
}

// Just the MaterialParams of an entity, as bytes. Used to assert the target's
// material DID move and a bystander's did NOT.
std::vector<std::byte> freeze_material(const aleph::lowering::LoweredEntity& e) {
    std::vector<std::byte> out;
    put_material(out, e.material);
    return out;
}

// The handle_map as bytes, walked in OrderedMap iteration order (NodeId -> index).
std::vector<std::byte> freeze_handle_map(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;
    put(out, static_cast<std::uint64_t>(ls.handle_map.size()));
    for (auto [nid, idx] : ls.handle_map) {
        put(out, nid.value);
        put(out, idx);
    }
    return out;
}

// The camera pose as bytes, field by field (padding-proof).
std::vector<std::byte> freeze_camera(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;
    put_vec3(out, ls.camera.look_from);
    put_vec3(out, ls.camera.look_at);
    put_vec3(out, ls.camera.up);
    put(out, ls.camera.vfov_deg);
    put(out, ls.camera.aperture);
    put(out, ls.camera.focus_dist);
    return out;
}

// Look up the entities index of `source` via the stable handle_map.
const aleph::lowering::LoweredEntity*
entity_for(const aleph::lowering::LoweredScene& ls, NodeId source) {
    const std::uint32_t* idx = ls.handle_map.get(source);
    if (idx == nullptr) return nullptr;
    return &ls.entities[*idx];
}

}  // namespace

TEST_CASE("lowering: SetMaterial edits ONLY the target entity; all else byte-identical") {
    TwoMesh s = make_two_mesh();

    // ── lower BEFORE the edit ────────────────────────────────────────────────
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& a = *before;

    // Pin the pre-state we will diff against: two entities, one light, both meshes
    // mapped, the target resolving the original red Lambertian.
    REQUIRE(a.entities.size() == 2);
    REQUIRE(a.lights.size() == 1);
    const aleph::lowering::LoweredEntity* a_target    = entity_for(a, s.mesh_a);
    const aleph::lowering::LoweredEntity* a_bystander = entity_for(a, s.mesh_b);
    REQUIRE(a_target != nullptr);
    REQUIRE(a_bystander != nullptr);
    REQUIRE(a_target->material.kind == MaterialKind::Lambertian);
    REQUIRE(a_target->material.albedo == Vec3{1.0f, 0.0f, 0.0f});

    // Freeze every survivor's bytes from the pre-state.
    const std::vector<std::byte> a_target_material = freeze_material(*a_target);
    const std::vector<std::byte> a_bystander_image = freeze_entity(*a_bystander);
    const std::vector<std::byte> a_light_image     = freeze_entity(a.lights[0]);
    const std::vector<std::byte> a_handle_map      = freeze_handle_map(a);
    const std::vector<std::byte> a_camera          = freeze_camera(a);

    // ── the edit: SetMaterial on the TARGET mesh -> a green Metal ────────────
    // SetMaterial is an attribute op (SPEC §5): a typed, validated, all-or-nothing
    // mutation of the GRAPH. It retargets the material the mesh References. We give
    // it the mesh id and the new material attributes — as a `MaterialParams`, the
    // SAME frozen, renderer-independent view the IR uses (SPEC §4.1) — and apply_op
    // mutates the referenced Material node and returns a RewriteRecord.
    aleph::lowering::MaterialParams edited{};
    edited.kind   = MaterialKind::Metal;
    edited.albedo = Vec3{0.0f, 1.0f, 0.0f};
    edited.fuzz   = 0.1f;
    edited.ior    = 1.5f;
    edited.emit   = Vec3{0, 0, 0};

    aleph::lowering::Op op = aleph::lowering::SetMaterial{s.mesh_a, edited};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // ── re-lower AFTER the edit ──────────────────────────────────────────────
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const aleph::lowering::LoweredScene& b = *after;

    // Structure is unchanged: still two entities, one light.
    REQUIRE(b.entities.size() == 2);
    REQUIRE(b.lights.size() == 1);

    const aleph::lowering::LoweredEntity* b_target    = entity_for(b, s.mesh_a);
    const aleph::lowering::LoweredEntity* b_bystander = entity_for(b, s.mesh_b);
    REQUIRE(b_target != nullptr);
    REQUIRE(b_bystander != nullptr);

    // ── (1) the target entity's MaterialParams CHANGED, to exactly the edit ──
    CHECK(freeze_material(*b_target) != a_target_material);
    CHECK(b_target->material.kind == MaterialKind::Metal);
    CHECK(b_target->material.albedo == Vec3{0.0f, 1.0f, 0.0f});
    CHECK(b_target->material.fuzz == doctest::Approx(0.1f));

    // ── (2) the target's IDENTITY and GEOMETRY did not move ──────────────────
    // Only the material is allowed to change: source id and world geometry stay put.
    CHECK(b_target->source == s.mesh_a);
    CHECK(std::holds_alternative<SphereLocal>(b_target->world_geometry));
    CHECK(std::get<SphereLocal>(b_target->world_geometry).center == Vec3{0, 0, 0});
    CHECK(std::get<SphereLocal>(b_target->world_geometry).radius == doctest::Approx(1.0f));

    // ── (3) EVERY OTHER handle is byte-identical across the re-lower ─────────
    // The bystander entity (id + geometry + material), the light table, the
    // camera pose, and the handle_map must all be bit-for-bit unchanged.
    CHECK(freeze_entity(*b_bystander) == a_bystander_image);
    CHECK(freeze_entity(b.lights[0]) == a_light_image);
    CHECK(freeze_camera(b) == a_camera);
    CHECK(freeze_handle_map(b) == a_handle_map);
}
