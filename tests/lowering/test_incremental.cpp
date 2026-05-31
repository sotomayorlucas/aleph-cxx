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
import aleph.dpo;
import aleph.lowering;

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise byte serializers

// Phase 5.x-c — Incremental Lowering (SPEC 2026-05-31). Tests 1-partial + 3.
//
//   After an editor `Op`, `lower_incremental(prev, after, op, rec, &stats)`
//   re-lowers ONLY what the op touched instead of rebuilding the whole
//   `LoweredScene`. The two contracts this file pins, from a NON-TRIVIAL graph
//   (N>=3 meshes + per-mesh materials + a nested transform hierarchy):
//
//   (1) THE ORACLE (SPEC §6.1, partial — the ATTRIBUTE ops `SetMaterial` /
//       `SetTransform`). `lower_incremental(...)` is BYTE-IDENTICAL to a full
//       `lower(after)`. Incremental is purely an OPTIMIZATION; it may never
//       diverge from full, ever (SPEC §1/§2). We freeze each LoweredScene into a
//       flat, padding-proof byte image (shared `lowering_freeze.hpp`, walking
//       scalar leaves in IR iteration order — entities, lights, camera, then the
//       handle_map in OrderedMap/insertion order) and compare the images with ==.
//       That is the literal "byte-identical LoweredScene" the SPEC demands, and
//       it pins insertion order + f32 bit-patterns.
//
//   (3) THE WORK BOUND (SPEC §6.3, "actually_incremental"). `IncrementalStats`
//       proves it isn't secretly full. A `SetMaterial` on ONE of N meshes
//       recomputes EXACTLY 1 entity (not N). A `SetTransform` on a sub-tree
//       Transform recomputes EXACTLY that sub-tree's mesh count (its
//       `Contains`-descendants), not the whole scene. Three distinct numbers —
//       1 (material), the sub-tree size, and N — give the oracle teeth: if
//       incremental secretly re-lowered everything, `recomputed_entities` would
//       be N in every case and these CHECKs would fail.
//
// Editing is a MORPHISM, not a mutation of the render product (SPEC §1): an
// editor gesture becomes an `Op` that mutates the GRAPH (the single source of
// truth), then we re-lower. So the loop under test is:
//   1. lower(g)                -> prev (LoweredScene before the op)
//   2. apply_op(g, op)         -> mutate the GRAPH; returns a RewriteRecord
//   3a. lower(g)               -> full oracle (LoweredScene after)
//   3b. lower_incremental(prev, g, op, rec, &stats) -> incremental result
//   assert: freeze(3b) == freeze(3a)  AND  stats.recomputed_entities is O(dirty).
//
// The `Op` carries the modified node for ATTRIBUTE ops (`SetMaterial`/
// `SetTransform` mutate in place, so the `RewriteRecord` has NO created/deleted
// entries for them — all four record vectors are empty). The `RewriteRecord`
// carries the STRUCTURAL deltas for the structural ops; those are covered in
// wave 2. Here `rec` is the (empty) record `apply_op` returned for the attribute
// op, threaded verbatim per the final signature.
//
// No exceptions (aleph_flags_isa): `lower`, `apply_op` and `lower_incremental`
// all return `std::expected`; we REQUIRE has_value() before trusting any
// post-state. Determinism (SPEC §7): the fixture is built in a fixed insertion
// order with f32 scalars, so both `lower()` and the incremental patch are
// byte-stable.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Mat4;
using aleph::math::Vec3;

namespace {

using aleph_test_freeze::freeze;

// A pure translation as a LocalTransform (keeps the world-composition math exact
// in f32, so a transform edit's effect is unambiguous).
LocalTransform translate(float x, float y, float z) {
    return LocalTransform{Mat4::translate(Vec3{x, y, z})};
}

// ── canonical N>=3-mesh enriched graph, with a NESTED transform sub-tree ──────
//
//   root  Transform  T(0,0,0)   Contains: cam, mesh_0, mid
//     ├── mesh_0  Sphere @ origin       -> mat_0  (red Lambertian)
//     └── mid  Transform  T(0,10,0)     Contains: mesh_1, mesh_2   <- SUB-TREE
//           ├── mesh_1  Sphere @ +2x    -> mat_1  (grey Metal)
//           └── mesh_2  Sphere @ +4x    -> mat_2  (Dielectric)
//   light  (Area)  Contained by root    -> the lone light-table member
//
// So:
//   * N = 3 meshes total (mesh_0, mesh_1, mesh_2) in deterministic insertion
//     order, each References its OWN material -> three entities.
//   * `mid` is a sub-tree Transform whose `Contains`-descendants are exactly
//     {mesh_1, mesh_2} (sub-tree size = 2). A `SetTransform` on `mid` shifts
//     both their world positions but NOTHING else, so incremental must recompute
//     exactly 2 entities — not 1, not 3.
//   * Editing mat_0 (the material mesh_0 References) touches exactly 1 entity.
//   * No material is emissive, so the light table is exactly the Light node and
//     must survive every attribute edit byte-for-byte.
struct ManyMesh {
    Graph  g;
    NodeId root{}, mid{}, cam{};
    NodeId mesh_0{}, mat_0{};
    NodeId mesh_1{}, mat_1{};
    NodeId mesh_2{}, mat_2{};
    NodeId light{};
};

ManyMesh make_many_mesh() {
    ManyMesh s;
    Graph& g = s.g;

    // root Transform at identity (no incoming Contains -> the root).
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    // Camera with a concrete pose (must survive every edit verbatim).
    s.cam = g.alloc_node_id();
    Camera cam{};
    cam.id        = s.cam;
    cam.sensor_id = std::string("sensor0");
    cam.look_from = Vec3{0, 0, 12};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 45.0f;
    g.insert_node(std::move(cam));

    // mesh_0 — directly under root — the SetMaterial TARGET.
    s.mesh_0 = g.alloc_node_id();
    Mesh mesh_0{s.mesh_0, std::string("sphere_0"), 0};
    mesh_0.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_0));

    s.mat_0 = g.alloc_node_id();
    Material mat_0{s.mat_0, MaterialKind::Lambertian};
    mat_0.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat_0.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_0));

    // mid Transform — the SUB-TREE root for the SetTransform target. Translated
    // up so its descendants have a non-trivial world position.
    s.mid = g.alloc_node_id();
    g.insert_node(Transform{s.mid, 1, translate(0, 10, 0)});

    // mesh_1 — under mid — a bystander for SetMaterial, in-subtree for SetTransform.
    s.mesh_1 = g.alloc_node_id();
    Mesh mesh_1{s.mesh_1, std::string("sphere_1"), 0};
    mesh_1.geometry = SphereLocal{Vec3{2, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_1));

    s.mat_1 = g.alloc_node_id();
    Material mat_1{s.mat_1, MaterialKind::Metal};
    mat_1.albedo = Vec3{0.5f, 0.5f, 0.5f};
    mat_1.fuzz   = 0.25f;
    mat_1.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_1));

    // mesh_2 — under mid — the other in-subtree mesh.
    s.mesh_2 = g.alloc_node_id();
    Mesh mesh_2{s.mesh_2, std::string("sphere_2"), 0};
    mesh_2.geometry = SphereLocal{Vec3{4, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_2));

    s.mat_2 = g.alloc_node_id();
    Material mat_2{s.mat_2, MaterialKind::Dielectric};
    mat_2.ior  = 1.5f;
    mat_2.emit = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_2));

    // explicit Light node — the lone light-table member (NOT emissive-material
    // driven, so attribute edits to the meshes never disturb it).
    s.light = g.alloc_node_id();
    Light light{};
    light.id       = s.light;
    light.kind     = LightKind::Area;
    light.emit_ref = std::string("emit0");
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 20, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    // Hierarchy (Contains in insertion order -> deterministic entity order).
    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.mesh_0);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.mid);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.light);
    (void)g.add_edge(EdgeKind::Contains, s.mid,  s.mesh_1);
    (void)g.add_edge(EdgeKind::Contains, s.mid,  s.mesh_2);
    // Each mesh References its own material (all resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh_0, s.mat_0);
    (void)g.add_edge(EdgeKind::References, s.mesh_1, s.mat_1);
    (void)g.add_edge(EdgeKind::References, s.mesh_2, s.mat_2);
    return s;
}

}  // namespace

// ── (1) + (3) for SetMaterial on ONE of N meshes ─────────────────────────────
//
// Gesture: retarget mesh_0's material to a green Metal. The dirty set is exactly
// {mesh_0's entity} — mesh_1/mesh_2 reference different materials and their world
// transforms are untouched, so incremental must recompute exactly 1 entity and
// still produce a scene byte-identical to a full re-lower.
TEST_CASE("incremental: SetMaterial on 1 of N meshes == full, recomputes exactly 1") {
    ManyMesh s = make_many_mesh();

    // ── lower BEFORE the edit -> prev ────────────────────────────────────────
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;

    // Pin the non-trivial pre-state: three entities, one light, three groups in
    // the handle_map — so "recompute exactly 1" is a meaningful claim against N=3.
    REQUIRE(prev.entities.size() == 3);
    REQUIRE(prev.lights.size() == 1);

    // ── the edit: SetMaterial on mesh_0 -> a green Metal ─────────────────────
    aleph::lowering::MaterialParams edited{};
    edited.kind   = MaterialKind::Metal;
    edited.albedo = Vec3{0.0f, 1.0f, 0.0f};
    edited.fuzz   = 0.1f;
    edited.ior    = 1.5f;
    edited.emit   = Vec3{0, 0, 0};

    aleph::lowering::Op op = aleph::lowering::SetMaterial{s.mesh_0, edited};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;
    // An attribute op creates/deletes nothing (SPEC §5: mirrors a DPO ModifyAttr).
    CHECK(rec.created_nodes.empty());
    CHECK(rec.deleted_nodes.empty());

    // ── (1) ORACLE: incremental == full, byte-identical ──────────────────────
    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    CHECK(freeze(*inc) == freeze(*full));

    // ── (3) WORK BOUND: exactly 1 entity recomputed (not N == 3) ─────────────
    CHECK(stats.recomputed_entities == 1);
    CHECK(stats.recomputed_entities != prev.entities.size());  // not full
    // A material-only edit on a non-emissive material leaves the light table
    // untouched, so the sheaf H⁰ pass need not rerun.
    CHECK_FALSE(stats.light_groups_recomputed);
}

// ── (1) + (3) for SetTransform on a SUB-TREE Transform ───────────────────────
//
// Gesture: shift `mid` (which Contains mesh_1 and mesh_2) by T(0,0,5). The dirty
// set is exactly mid's `Contains`-descendants = {mesh_1, mesh_2} (sub-tree size
// = 2): both their WORLD geometries move; mesh_0 (outside the sub-tree), the
// camera and the light are untouched. Incremental must recompute exactly 2
// entities and still match a full re-lower byte-for-byte.
TEST_CASE("incremental: SetTransform on a sub-tree == full, recomputes the sub-tree only") {
    ManyMesh s = make_many_mesh();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;
    REQUIRE(prev.entities.size() == 3);  // N = 3
    REQUIRE(prev.lights.size() == 1);

    // ── the edit: SetTransform on `mid` -> shift the sub-tree by +5z ─────────
    // Compose onto the existing T(0,10,0) so the new local is T(0,10,5); both
    // descendants' world positions shift, and only those two.
    aleph::lowering::Op op =
        aleph::lowering::SetTransform{s.mid, translate(0, 10, 5)};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;
    CHECK(rec.created_nodes.empty());
    CHECK(rec.deleted_nodes.empty());

    // ── (1) ORACLE: incremental == full, byte-identical ──────────────────────
    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    CHECK(freeze(*inc) == freeze(*full));

    // ── (3) WORK BOUND: exactly the sub-tree (2 entities), not all N == 3 ────
    CHECK(stats.recomputed_entities == 2);
    CHECK(stats.recomputed_entities != prev.entities.size());  // not full
    // A transform edit does not touch any Influences edge / Light / emission, so
    // the light-group table is reused — no sheaf recompute.
    CHECK_FALSE(stats.light_groups_recomputed);
}
