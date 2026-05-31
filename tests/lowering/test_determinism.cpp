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

// SPEC §8.8 — determinism.
//
//   "lower -> edit (an Op) -> lower yields a deterministic, consistent
//    handle_map; the LoweredScene of identical graphs is byte-identical."
//
// This is the determinism CONTRACT (SPEC §7): the typed scene graph is the
// single source of truth and `lower()` is a deterministic functor — same graph
// in, byte-identical LoweredScene out. Editing is a MORPHISM on the GRAPH (an
// `Op`), never a mutation of the render product (SPEC §1); after an edit we
// re-lower, and the derived IR must STILL be deterministic and its handle_map
// STILL consistent.
//
// Three things this file pins, distinct from the other §8 tests:
//   (A) Same graph -> byte-identical LoweredScene across two independent lower()
//       calls (re-asserted here as the determinism anchor; test_lower_minimal
//       checks the minimal scene, here we use a richer multi-entity scene so the
//       byte image exercises insertion order across several entities, a light
//       table member, the camera, and a multi-slot handle_map).
//   (B) TWO INDEPENDENTLY BUILT graphs that are structurally identical (same
//       construction sequence, hence the same NodeIds and insertion order) lower
//       to byte-identical LoweredScenes. Determinism is a property of the graph
//       VALUE, not of one Graph object's address/history.
//   (C) The lower -> edit(Op) -> lower loop: after an `Op` mutates the GRAPH and
//       we re-lower, the handle_map is deterministic and CONSISTENT (every key a
//       live node, every value an in-range entities slot sourced from that key,
//       1:1), and re-lowering the EDITED graph twice is itself byte-identical.
//       Survivors keep stable handles; an added entity gets a fresh stable slot.
//
// LoweredScene holds a move-only OrderedMap (handle_map) and deletes copy, so it
// is neither copyable nor trivially comparable. We freeze each lowering into a
// flat byte image — POD fields walked in IR iteration order — and compare images
// with ==. Fields are walked EXPLICITLY (not memcpy'd whole structs) because the
// IR's value types embed `aleph::math::Vec3` (alignas(16)), so whole-struct
// copies would compare INDETERMINATE padding bytes that `lower()`'s aggregate
// init does not zero. Walking fields compares exactly the semantic state and is
// padding-proof — the same technique test_lower_minimal / test_edit_material /
// test_dpo_edit use, so "byte-identical" is consistent across the suite.
//
// No exceptions (aleph_flags_isa): lower() and apply_op() both return
// std::expected; we REQUIRE has_value() before trusting any post-state, and we
// also pin the two structured errors §8 cares about (DanglingReference, NoCamera)
// so the determinism guarantee is paired with loud, non-silent failure.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical multi-entity enriched graph ────────────────────────────────────
// root Transform (identity) Contains: a Camera, mesh_a (-> mat_a, red Lambertian),
// mesh_b (-> mat_b, metal grey), and an explicit Light. Two meshes => two entities
// in deterministic insertion order + a multi-slot handle_map; the Light is the
// lone light-table member. Built by a fixed construction sequence so that two
// independent calls mint identical NodeIds and identical insertion order.
struct Scene {
    Graph  g;
    NodeId root{}, cam{}, mesh_a{}, mat_a{}, mesh_b{}, mat_b{}, light{};
};

Scene make_scene() {
    Scene s;
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

    // mesh_a — unit sphere at the origin.
    s.mesh_a = g.alloc_node_id();
    Mesh mesh_a{s.mesh_a, std::string("sphere_a"), 0};
    mesh_a.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_a));

    // mat_a — red Lambertian, NOT emissive.
    s.mat_a = g.alloc_node_id();
    Material mat_a{s.mat_a, MaterialKind::Lambertian};
    mat_a.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat_a.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_a));

    // mesh_b — unit sphere at +2x.
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

    // Hierarchy (fixed insertion order -> deterministic traversal).
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_a);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh_b);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    // Each mesh references its own material (both must resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh_a, s.mat_a);
    (void)g.add_edge(EdgeKind::References, s.mesh_b, s.mat_b);
    return s;
}

// ── byte-image serializers for the frozen IR (padding-proof, field-wise) ─────
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

// A geometry primitive, field by field, after the variant tag (so a
// Sphere/Quad/Tri can never collide on payload bytes alone).
void put_geometry(std::vector<std::byte>& out,
                  const aleph::types::GeometryPayload& g) {
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

// One entity (or light-table entry): source id, world geometry, MaterialParams.
void put_entity(std::vector<std::byte>& out,
                const aleph::lowering::LoweredEntity& e) {
    put(out, e.source.value);
    put_geometry(out, e.world_geometry);
    put_material(out, e.material);
}

// Freeze a whole LoweredScene into a flat byte image, walking everything in IR
// iteration order: entities, the light table, the camera pose, then the
// handle_map in OrderedMap iteration (insertion) order. This is the literal
// "byte-identical LoweredScene" the SPEC demands; it also pins insertion order
// and f32 bit-patterns.
std::vector<std::byte> freeze(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;

    put(out, static_cast<std::uint64_t>(ls.entities.size()));
    for (const auto& e : ls.entities) put_entity(out, e);

    put(out, static_cast<std::uint64_t>(ls.lights.size()));
    for (const auto& e : ls.lights) put_entity(out, e);

    put_vec3(out, ls.camera.look_from);
    put_vec3(out, ls.camera.look_at);
    put_vec3(out, ls.camera.up);
    put(out, ls.camera.vfov_deg);
    put(out, ls.camera.aperture);
    put(out, ls.camera.focus_dist);

    put(out, static_cast<std::uint64_t>(ls.handle_map.size()));
    for (auto [nid, idx] : ls.handle_map) {
        put(out, nid.value);
        put(out, idx);
    }
    return out;
}

// Just the handle_map as bytes, walked in OrderedMap iteration order. The
// determinism contract pins the handle_map specifically (SPEC §7/§8.8), so we
// also diff it in isolation.
std::vector<std::byte> freeze_handle_map(const aleph::lowering::LoweredScene& ls) {
    std::vector<std::byte> out;
    put(out, static_cast<std::uint64_t>(ls.handle_map.size()));
    for (auto [nid, idx] : ls.handle_map) {
        put(out, nid.value);
        put(out, idx);
    }
    return out;
}

// ── handle_map consistency oracle (the "consistent handle_map" half of §8.8) ──
// Against the live graph `g` the IR was lowered from:
//   * one map slot per entity (1:1 size),
//   * every key is a live graph node,
//   * every value indexes a real `entities` slot,
//   * that slot is sourced from exactly that key,
//   * every entity is reachable through the map (no orphaned slot),
//   * and each entity's source resolves back through the map to itself.
[[nodiscard]] bool handles_consistent(const Graph& g,
                                      const aleph::lowering::LoweredScene& ls) {
    if (ls.handle_map.size() != ls.entities.size()) return false;

    std::vector<bool> covered(ls.entities.size(), false);
    for (auto [nid, idx] : ls.handle_map) {
        if (g.node(nid) == nullptr) return false;
        if (idx >= ls.entities.size()) return false;
        if (ls.entities[idx].source != nid) return false;
        covered[idx] = true;
    }
    for (bool c : covered) if (!c) return false;
    for (const auto& e : ls.entities) {
        const std::uint32_t* idx = ls.handle_map.get(e.source);
        if (idx == nullptr) return false;
        if (&ls.entities[*idx] != &e) return false;
    }
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// (A) Determinism anchor: same graph -> byte-identical LoweredScene + a stable,
//     consistent handle_map across two independent lower() calls.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: same graph lowers byte-identically (determinism anchor)") {
    Scene s = make_scene();

    auto a = aleph::lowering::lower(s.g);
    auto b = aleph::lowering::lower(s.g);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    // Two entities, one light, a two-slot handle_map — a richer image than the
    // minimal scene, so insertion order across several entities is exercised.
    REQUIRE(a->entities.size() == 2);
    REQUIRE(a->lights.size() == 1);
    REQUIRE(a->handle_map.size() == 2);

    // The handle_map is consistent against the graph it derived from.
    CHECK(handles_consistent(s.g, *a));

    // The whole LoweredScene is bit-for-bit identical across the two lowerings.
    const std::vector<std::byte> img_a = freeze(*a);
    const std::vector<std::byte> img_b = freeze(*b);
    REQUIRE(img_a.size() == img_b.size());
    CHECK(img_a == img_b);

    // The handle_map in isolation is identical too (insertion order is stable).
    CHECK(freeze_handle_map(*a) == freeze_handle_map(*b));
}

// ─────────────────────────────────────────────────────────────────────────────
// (B) Determinism is a property of the graph VALUE, not the Graph object:
//     two independently built, structurally identical graphs lower identically.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: two independently built identical graphs lower byte-identically") {
    Scene s1 = make_scene();
    Scene s2 = make_scene();

    // The two builds mint the same NodeIds in the same insertion order.
    REQUIRE(s1.mesh_a == s2.mesh_a);
    REQUIRE(s1.mesh_b == s2.mesh_b);
    REQUIRE(s1.light  == s2.light);

    auto l1 = aleph::lowering::lower(s1.g);
    auto l2 = aleph::lowering::lower(s2.g);
    REQUIRE(l1.has_value());
    REQUIRE(l2.has_value());

    const std::vector<std::byte> img1 = freeze(*l1);
    const std::vector<std::byte> img2 = freeze(*l2);
    REQUIRE(img1.size() == img2.size());
    CHECK(img1 == img2);
}

// ─────────────────────────────────────────────────────────────────────────────
// (C) The loop: lower -> edit(an Op) -> lower yields a deterministic, consistent
//     handle_map. An AddObject mutates the GRAPH; re-lowering is deterministic
//     (re-lower twice -> byte-identical), the handle_map stays consistent, and
//     survivors keep their stable slots while the new entity gets a fresh one.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: lower -> edit(AddObject) -> lower has a deterministic, consistent handle_map") {
    Scene s = make_scene();

    // ── lower BEFORE the edit ────────────────────────────────────────────────
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    REQUIRE(before->entities.size() == 2);
    REQUIRE(handles_consistent(s.g, *before));

    // Pin the survivors' stable handles from the pre-state.
    const std::uint32_t* a_idx_before = before->handle_map.get(s.mesh_a);
    const std::uint32_t* b_idx_before = before->handle_map.get(s.mesh_b);
    REQUIRE(a_idx_before != nullptr);
    REQUIRE(b_idx_before != nullptr);
    const std::uint32_t a_slot = *a_idx_before;
    const std::uint32_t b_slot = *b_idx_before;

    // ── the edit: AddObject under the root (a green Lambertian sphere) ───────
    // AddObject is a STRUCTURAL op (SPEC §5): transactional, all-or-nothing. It
    // mutates the GRAPH (the single source of truth) — creating a Mesh + Material
    // + References + parent Contains — after which the caller re-lowers. The new
    // mesh's NodeId is reported in the RewriteRecord's created_nodes.
    aleph::lowering::MaterialParams mp{};
    mp.kind   = MaterialKind::Lambertian;
    mp.albedo = Vec3{0.0f, 1.0f, 0.0f};
    mp.emit   = Vec3{0, 0, 0};

    aleph::lowering::Op op =
        aleph::lowering::AddObject{s.root, SphereLocal{Vec3{0, 2, 0}, 0.5f}, mp};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    REQUIRE(applied->created_nodes.size() == 2);  // [mesh, material]
    const NodeId new_mesh = applied->created_nodes.front();

    // ── re-lower AFTER the edit, TWICE ───────────────────────────────────────
    auto after1 = aleph::lowering::lower(s.g);
    auto after2 = aleph::lowering::lower(s.g);
    REQUIRE(after1.has_value());
    REQUIRE(after2.has_value());
    const aleph::lowering::LoweredScene& c = *after1;

    // The entity count grew by exactly one; the handle_map tracks it 1:1.
    CHECK(c.entities.size() == 3);
    CHECK(c.handle_map.size() == 3);

    // (consistent) the post-edit handle_map is fully consistent with the GRAPH.
    CHECK(handles_consistent(s.g, c));

    // (survivor stability) both original meshes keep their EXACT pre-edit slots —
    // the determinism contract: a re-lower must not renumber survivors (SPEC §7).
    const std::uint32_t* a_idx_after = c.handle_map.get(s.mesh_a);
    const std::uint32_t* b_idx_after = c.handle_map.get(s.mesh_b);
    REQUIRE(a_idx_after != nullptr);
    REQUIRE(b_idx_after != nullptr);
    CHECK(*a_idx_after == a_slot);
    CHECK(*b_idx_after == b_slot);
    CHECK(c.entities[*a_idx_after].source == s.mesh_a);
    CHECK(c.entities[*b_idx_after].source == s.mesh_b);

    // (new handle) the added object got a fresh, in-range, self-consistent slot.
    const std::uint32_t* new_idx = c.handle_map.get(new_mesh);
    REQUIRE(new_idx != nullptr);
    CHECK(*new_idx < c.entities.size());
    CHECK(c.entities[*new_idx].source == new_mesh);

    // (deterministic) re-lowering the EDITED graph is itself byte-identical, so
    // the whole lower -> edit -> lower loop lands on a deterministic IR.
    const std::vector<std::byte> img1 = freeze(*after1);
    const std::vector<std::byte> img2 = freeze(*after2);
    REQUIRE(img1.size() == img2.size());
    CHECK(img1 == img2);
}

// ─────────────────────────────────────────────────────────────────────────────
// (C') The loop with an attribute op: lower -> edit(SetTransform) -> lower keeps
//      the handle_map deterministic and consistent. An attribute edit creates /
//      deletes no node, so the handle_map must be IDENTICAL across the edit even
//      though the world geometry moves.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: lower -> edit(SetTransform) -> lower keeps an identical, consistent handle_map") {
    Scene s = make_scene();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const std::vector<std::byte> hm_before = freeze_handle_map(*before);

    // SetTransform on the root: re-pose the whole hierarchy. The Contains edges
    // (and thus every node's identity) are untouched — only the local pose moves
    // — so the handle_map must be byte-identical after the re-lower, while the
    // entities' world geometry is recomputed off the new local.
    aleph::lowering::Op op = aleph::lowering::SetTransform{
        s.root,
        LocalTransform{aleph::math::Mat4::identity()}};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    auto after1 = aleph::lowering::lower(s.g);
    auto after2 = aleph::lowering::lower(s.g);
    REQUIRE(after1.has_value());
    REQUIRE(after2.has_value());

    // The handle_map is unchanged by an attribute edit (no node created/deleted).
    CHECK(freeze_handle_map(*after1) == hm_before);
    // ...and consistent against the post-edit graph.
    CHECK(handles_consistent(s.g, *after1));
    // ...and the re-lower is deterministic (two re-lowers byte-identical).
    CHECK(freeze(*after1) == freeze(*after2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Structured-error coverage (SPEC §4.2 / §8.4 / §9): determinism is paired with
// LOUD failure — a broken graph fails with a structured LowerError, deterministic
// and never a silent default. These complement the value path above.
// ─────────────────────────────────────────────────────────────────────────────

// DanglingReference: a Mesh with no References->Material edge must fail HERE, and
// must fail the SAME way every time (deterministic structured error).
TEST_CASE("lowering: a dangling Mesh->Material reference deterministically yields DanglingReference") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("sensor0")});

    // A Mesh with a geometry payload but NO References->Material edge.
    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("sphere"), 0};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));

    // A Material exists in the graph, but is never wired to the mesh -> still
    // dangling (the reference, not merely the node, must be present).
    const NodeId mat = g.alloc_node_id();
    g.insert_node(Material{mat, MaterialKind::Lambertian});

    (void)g.add_edge(EdgeKind::Contains, root, cam);
    (void)g.add_edge(EdgeKind::Contains, root, mesh);
    // Intentionally NO References: mesh -> mat.

    auto a = aleph::lowering::lower(g);
    auto b = aleph::lowering::lower(g);
    // Must NOT silently succeed with a default material...
    REQUIRE_FALSE(a.has_value());
    REQUIRE_FALSE(b.has_value());
    // ...and the structured error is precisely DanglingReference, every time.
    CHECK(a.error() == aleph::lowering::LowerError::DanglingReference);
    CHECK(b.error() == aleph::lowering::LowerError::DanglingReference);
    CHECK(a.error() == b.error());
}

// NoCamera: a camera-less graph fails fast with NoCamera (CameraExclusive),
// deterministically — never a silent value with a default camera.
TEST_CASE("lowering: a camera-less graph deterministically yields NoCamera") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    // A fully-wired, otherwise-lowerable Mesh+Material — but NO Camera node.
    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("sphere"), 0};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));

    const NodeId mat = g.alloc_node_id();
    Material mat_node{mat, MaterialKind::Lambertian};
    mat_node.albedo = Vec3{1.0f, 0.0f, 0.0f};
    g.insert_node(std::move(mat_node));

    (void)g.add_edge(EdgeKind::Contains,   root, mesh);
    (void)g.add_edge(EdgeKind::References, mesh, mat);

    auto a = aleph::lowering::lower(g);
    auto b = aleph::lowering::lower(g);
    REQUIRE_FALSE(a.has_value());
    REQUIRE_FALSE(b.has_value());
    CHECK(a.error() == aleph::lowering::LowerError::NoCamera);
    CHECK(b.error() == aleph::lowering::LowerError::NoCamera);
    CHECK(a.error() == b.error());
}
