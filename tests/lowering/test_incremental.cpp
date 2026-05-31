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
import aleph.dpo;       // RewriteRecord, Rule, rules::refine_cell
import aleph.lowering;

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise byte serializers

// Phase 5.x-c — Incremental Lowering (SPEC 2026-05-31). Tests 1-partial + 3
// (this file's original scope, W1), EXTENDED with SPEC §6 tests 2, 4, 5 (W2):
//
//   (2) property_256 (SPEC §6.2) — the OACLE at SCALE. 256 pseudo-random graphs
//       (a seeded, deterministic LCG: no <random>, reproducible byte-for-byte on
//       every run), each exercised with ONE random Op of EACH supported kind
//       (SetMaterial, SetTransform, AddObject, AddLight, DeleteObject, ApplyRule).
//       For every (graph, op) pair, `lower_incremental(prev, after, op, rec)` is
//       BYTE-IDENTICAL to `lower(after)` — proving the §1/§2 contract is not a
//       fixture artifact but holds across a wide, randomized population. Because
//       `apply_op` MUTATES the graph and `Graph`/`LoweredScene` are move-only, we
//       REBUILD an identical graph from the same seed for each op kind so the ops
//       are independent (one op per fresh copy), per the SPEC's "one random Op
//       each" phrasing.
//
//   (4) light_groups_incremental (SPEC §6.4) — the sheaf H⁰ reuse contract, with
//       teeth, on a graph whose `light_groups` are NON-TRIVIAL (real Influences
//       edges, so the partition is more than one singleton). An op that touches
//       NEITHER Influences NOR emission (a SetTransform) MUST reuse the table:
//       `stats.light_groups_recomputed == false` AND `inc.light_groups ==
//       prev.light_groups` (no sheaf recompute at all). An op that DOES change
//       emission (a SetMaterial that lights a previously-dark mesh) MUST recompute:
//       `stats.light_groups_recomputed == true` AND `inc.light_groups` equals a
//       full `lower(after)`'s.
//
//   (5) applyrule_fallback (SPEC §6.5) — `ApplyRule` is not yet dirty-set
//       incremental, so `lower_incremental` FALLS BACK to a full `lower(after)`
//       (byte-identical by construction). The result must be byte-identical to
//       full AND have NO DANGLING HANDLES: every `handle_map` key resolves to an
//       entity whose `source` is that key, the map size equals the entity count,
//       and every key names a live Mesh in the post-graph.
//
// All three reuse the SAME padding-proof `freeze` oracle (lowering_freeze.hpp) and
// the no-exceptions / determinism discipline the W1 cases established.
//
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

// ─────────────────────────────────────────────────────────────────────────────
// W2 ADDITIONS — SPEC §6 tests 2 (property_256), 4 (light_groups), 5 (applyrule).
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ── a tiny, seeded, deterministic LCG (no <random>) ──────────────────────────
//
// We need a reproducible pseudo-random stream so the 256-graph property sweep is
// byte-for-byte stable across runs (SPEC §6.2 "seeded, deterministic"; §7 "3x
// determinism guard"). A 64-bit LCG (Numerical Recipes constants) is more than
// enough entropy for fixture shape, and — unlike <random>, whose engines/
// distributions are implementation-defined — its sequence is fixed by the C++
// standard's integer arithmetic, so the SAME seed yields the SAME graphs on every
// platform/run. We deliberately avoid <random> AND <algorithm> (the modules <=>
// mangling clash this directory documents).
struct Lcg {
    std::uint64_t state;
    explicit Lcg(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }
    // Uniform-ish in [0, n) for small n (modulo bias is irrelevant for fixtures).
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((next() >> 33) % n);
    }
    // A bounded f32 in [-range, +range], quantized so the value is exactly
    // representable and the freeze comparison is unambiguous.
    float real(float range) {
        const auto q = static_cast<std::int32_t>(below(2001)) - 1000;  // [-1000,1000]
        return (static_cast<float>(q) / 1000.0f) * range;
    }
};

// NO DANGLING HANDLES (SPEC §6.5): every handle_map key resolves to an entity
// whose `source` is that key; the map size equals the entity count; and every key
// names a live Mesh in the post-graph. This is the literal "no dangling handles"
// the SPEC demands of the ApplyRule fallback (and a good invariant for any scene).
[[nodiscard]] bool no_dangling_handles(const aleph::graph::Graph& g,
                                       const aleph::lowering::LoweredScene& ls) {
    if (ls.handle_map.size() != ls.entities.size()) return false;
    for (auto [src, idx] : ls.handle_map) {
        if (idx >= ls.entities.size()) return false;             // index in range
        if (ls.entities[idx].source != src) return false;        // round-trips
        const aleph::types::Node* n = g.node(src);               // live node
        if (n == nullptr) return false;
        if (aleph::types::kind_of(*n) != aleph::types::NodeKind::Mesh) return false;
    }
    // And every entity is reachable through the map (bijection entities<->keys).
    for (const auto& e : ls.entities) {
        if (!ls.handle_map.contains(e.source)) return false;
    }
    return true;
}

// ── a deterministic random graph for the property sweep ──────────────────────
//
// Shape (a valid, lowerable scene): a root Transform (identity) Contains a Camera,
// a nested child Transform `mid`, between 1 and 4 Meshes (each with its OWN
// Material, References-wired; some split under `mid` for a non-trivial transform
// hierarchy), and 0..2 explicit Lights. The PSEUDO-RANDOM bits driving counts,
// geometry, material kinds/params and emission come from the LCG, so the graph is
// a pure function of the seed. We also surface the specific NodeIds an op needs
// (a mesh to retarget/delete, the `mid` transform to move, a parent to add under)
// so the property loop can build a well-typed Op for each kind.
struct RandGraph {
    Graph  g;
    NodeId root{}, mid{};
    NodeId a_mesh{};   // a Mesh that exists (SetMaterial / DeleteObject target)
    NodeId mid_xf{};   // the `mid` Transform (SetTransform target — has descendants)
};

RandGraph make_random_graph(std::uint64_t seed) {
    Lcg rng(seed);
    RandGraph s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    Camera cam{};
    cam.id        = g.alloc_node_id();
    cam.sensor_id = std::string("sensor");
    cam.look_from = Vec3{rng.real(5), rng.real(5), 8 + rng.real(2)};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 30.0f + rng.real(10);
    g.insert_node(std::move(cam));

    // A nested child Transform with a random pose -> a real transform hierarchy.
    s.mid    = g.alloc_node_id();
    s.mid_xf = s.mid;
    g.insert_node(Transform{s.mid, 1, translate(rng.real(6), rng.real(6), rng.real(6))});

    (void)g.add_edge(EdgeKind::Contains, s.root, cam.id);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.mid);

    const std::uint32_t n_mesh = 1 + rng.below(4);  // 1..4 meshes
    bool have_a = false;
    for (std::uint32_t i = 0; i < n_mesh; ++i) {
        const NodeId mesh_id = g.alloc_node_id();
        Mesh mesh{mesh_id, std::string("m"), 0};
        mesh.geometry = SphereLocal{Vec3{rng.real(4), rng.real(4), rng.real(4)},
                                    0.5f + (static_cast<float>(rng.below(20)) / 10.0f)};
        g.insert_node(std::move(mesh));

        const NodeId mat_id = g.alloc_node_id();
        const std::uint32_t mk = rng.below(3);  // Lambertian / Metal / Dielectric
        Material mat{mat_id,
                     mk == 0 ? MaterialKind::Lambertian
                             : (mk == 1 ? MaterialKind::Metal : MaterialKind::Dielectric)};
        mat.albedo = Vec3{static_cast<float>(rng.below(11)) / 10.0f,
                          static_cast<float>(rng.below(11)) / 10.0f,
                          static_cast<float>(rng.below(11)) / 10.0f};
        mat.fuzz = static_cast<float>(rng.below(10)) / 10.0f;
        mat.ior  = 1.0f + static_cast<float>(rng.below(10)) / 10.0f;
        mat.emit = Vec3{0, 0, 0};  // non-emissive: keeps the light table stable
        g.insert_node(std::move(mat));

        // Half the meshes hang under `mid` (descendants), half under root.
        const NodeId parent = (rng.below(2) == 0) ? s.mid : s.root;
        (void)g.add_edge(EdgeKind::Contains,   parent,  mesh_id);
        (void)g.add_edge(EdgeKind::References, mesh_id, mat_id);

        if (!have_a) { s.a_mesh = mesh_id; have_a = true; }
    }

    // 0..2 explicit Lights under root (their own light-table members).
    const std::uint32_t n_light = rng.below(3);
    for (std::uint32_t i = 0; i < n_light; ++i) {
        const NodeId light_id = g.alloc_node_id();
        Light light{};
        light.id       = light_id;
        light.kind     = LightKind::Area;
        light.emit_ref = std::string("e");
        light.emission = Vec3{4, 4, 4};
        light.geometry = QuadLocal{Vec3{rng.real(3), 10, rng.real(3)},
                                   Vec3{2, 0, 0}, Vec3{0, 0, 2}};
        g.insert_node(std::move(light));
        (void)g.add_edge(EdgeKind::Contains, s.root, light_id);
    }

    return s;
}

}  // namespace

// ── (2) PROPERTY: 256 seeded random graphs × one Op of each kind == full ──────
//
// SPEC §6.2. For each of 256 deterministic graphs, exercise ONE random Op of each
// supported kind on a FRESH identical rebuild (the ops are independent — apply_op
// mutates and the scene types are move-only). Every (graph, op) pair must satisfy
// the CORE CONTRACT: `lower_incremental(prev, after, op, rec)` is BYTE-IDENTICAL
// to `lower(after)`. If incremental ever diverged from full on even one of the
// ~1500 cases, the freeze == would catch it. Seeded + deterministic, so a failure
// is reproducible by seed.
TEST_CASE("incremental: property — 256 random graphs, every op kind == full byte-identical") {
    // Run twice from the same base seed to also exercise the §7 determinism guard:
    // both passes must produce identical pass/fail behavior (the LCG is pure).
    for (std::uint64_t base : {0x5eed1234ABCDEF01ULL, 0x5eed1234ABCDEF01ULL}) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            const std::uint64_t seed = base ^ (0x9E3779B97F4A7C15ULL * (i + 1));

            // The six supported op KINDS (SPEC §5). For each we rebuild an
            // identical graph from `seed`, lower it (prev), apply the op
            // (after+rec), and compare incremental vs full.
            for (std::uint32_t kind = 0; kind < 6; ++kind) {
                RandGraph s = make_random_graph(seed);

                auto before = aleph::lowering::lower(s.g);
                REQUIRE(before.has_value());
                const aleph::lowering::LoweredScene& prev = *before;

                // Build a well-typed Op for this kind against the live graph.
                aleph::lowering::Op op = [&]() -> aleph::lowering::Op {
                    switch (kind) {
                        case 0: {  // SetMaterial on an existing mesh
                            aleph::lowering::MaterialParams p{};
                            p.kind   = MaterialKind::Metal;
                            p.albedo = Vec3{0.3f, 0.6f, 0.9f};
                            p.fuzz   = 0.2f;
                            p.ior    = 1.4f;
                            p.emit   = Vec3{0, 0, 0};  // non-emit: light table stable
                            return aleph::lowering::SetMaterial{s.a_mesh, p};
                        }
                        case 1:  // SetTransform on the nested `mid` Transform
                            return aleph::lowering::SetTransform{
                                s.mid_xf, translate(1.0f, -2.0f, 3.0f)};
                        case 2: {  // AddObject under root
                            aleph::lowering::AddObject add{};
                            add.parent   = s.root;
                            add.geometry = SphereLocal{Vec3{7, 7, 7}, 0.75f};
                            add.material = aleph::lowering::MaterialParams{};
                            return add;
                        }
                        case 3: {  // AddLight under root
                            aleph::lowering::AddLight add{};
                            add.parent   = s.root;
                            add.kind     = LightKind::Point;
                            add.emission = Vec3{3, 3, 3};
                            add.geometry = SphereLocal{Vec3{0, 9, 0}, 0.2f};
                            return add;
                        }
                        case 4:  // DeleteObject of an existing mesh
                            return aleph::lowering::DeleteObject{s.a_mesh};
                        default:  // ApplyRule (refine_cell — always matches a Mesh+Material)
                            return aleph::lowering::ApplyRule{
                                &aleph::dpo::rules::refine_cell()};
                    }
                }();

                auto applied = aleph::lowering::apply_op(s.g, op);
                REQUIRE(applied.has_value());
                const aleph::dpo::RewriteRecord& rec = *applied;

                auto full = aleph::lowering::lower(s.g);
                REQUIRE(full.has_value());

                auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec);
                REQUIRE(inc.has_value());

                // THE ORACLE, at scale: incremental == full, byte-for-byte.
                CHECK(freeze(*inc) == freeze(*full));
            }
        }
    }
}

// ── light-grouping fixture WITH Influences edges (non-trivial light_groups) ───
//
//   root Transform  Contains: cam, mid, l0, l1
//     └── mid Transform  Contains: mesh_0, mesh_1
//   l0 —Influences→ mesh_0 ;  l1 —Influences→ mesh_1
//   mesh_0 —Adjacent→ mesh_1   (so they are ONE connected region)
//
// Because mesh_0 and mesh_1 are Adjacent (one H⁰ component) and l0/l1 influence
// that region, `light_groups_of` groups {l0, l1} TOGETHER — a non-trivial
// partition (more than a bare singleton), so "reuse prev.light_groups verbatim"
// is a real claim, not a vacuous one. Neither mesh's material is emissive, so the
// only light-table members are l0 and l1.
namespace {

struct LitScene {
    Graph  g;
    NodeId root{}, mid{}, cam{};
    NodeId mesh_0{}, mat_0{}, mesh_1{}, mat_1{};
    NodeId l0{}, l1{};
};

LitScene make_lit_scene() {
    LitScene s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    s.cam = g.alloc_node_id();
    Camera cam{};
    cam.id        = s.cam;
    cam.sensor_id = std::string("sensor0");
    cam.look_from = Vec3{0, 0, 10};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    s.mid = g.alloc_node_id();
    g.insert_node(Transform{s.mid, 1, translate(0, 5, 0)});

    s.mesh_0 = g.alloc_node_id();
    Mesh mesh_0{s.mesh_0, std::string("m0"), 0};
    mesh_0.geometry = SphereLocal{Vec3{-1, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_0));

    s.mat_0 = g.alloc_node_id();
    Material mat_0{s.mat_0, MaterialKind::Lambertian};
    mat_0.albedo = Vec3{0.8f, 0.2f, 0.2f};
    mat_0.emit   = Vec3{0, 0, 0};  // dark — the SetMaterial test will light it up
    g.insert_node(std::move(mat_0));

    s.mesh_1 = g.alloc_node_id();
    Mesh mesh_1{s.mesh_1, std::string("m1"), 0};
    mesh_1.geometry = SphereLocal{Vec3{1, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh_1));

    s.mat_1 = g.alloc_node_id();
    Material mat_1{s.mat_1, MaterialKind::Metal};
    mat_1.albedo = Vec3{0.6f, 0.6f, 0.6f};
    mat_1.fuzz   = 0.1f;
    mat_1.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat_1));

    s.l0 = g.alloc_node_id();
    Light l0{};
    l0.id = s.l0; l0.kind = LightKind::Point; l0.emit_ref = std::string("e0");
    l0.emission = Vec3{5, 5, 5};
    l0.geometry = SphereLocal{Vec3{-3, 4, 0}, 0.2f};
    g.insert_node(std::move(l0));

    s.l1 = g.alloc_node_id();
    Light l1{};
    l1.id = s.l1; l1.kind = LightKind::Point; l1.emit_ref = std::string("e1");
    l1.emission = Vec3{5, 5, 5};
    l1.geometry = SphereLocal{Vec3{3, 4, 0}, 0.2f};
    g.insert_node(std::move(l1));

    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.mid);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.l0);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.l1);
    (void)g.add_edge(EdgeKind::Contains, s.mid,  s.mesh_0);
    (void)g.add_edge(EdgeKind::Contains, s.mid,  s.mesh_1);
    (void)g.add_edge(EdgeKind::References, s.mesh_0, s.mat_0);
    (void)g.add_edge(EdgeKind::References, s.mesh_1, s.mat_1);
    // The two meshes form ONE connected region (Adjacent), co-influenced by l0,l1.
    (void)g.add_edge(EdgeKind::Adjacent,   s.mesh_0, s.mesh_1);
    (void)g.add_edge(EdgeKind::Influences, s.l0, s.mesh_0);
    (void)g.add_edge(EdgeKind::Influences, s.l1, s.mesh_1);
    return s;
}

}  // namespace

// ── (4a) LIGHT GROUPS — an op NOT touching Influences/emission reuses the table ─
//
// SPEC §6.4 (the reuse half). A SetTransform on `mid` shifts mesh_0/mesh_1 world
// geometry but touches NO Influences edge, NO Light, NO emission. The sheaf H⁰
// pass must therefore NOT rerun (`light_groups_recomputed == false`) and the
// returned `light_groups` must be the PREVIOUS table verbatim — proven against a
// non-trivial grouping ({l0,l1} as one group, from the Adjacent+Influences scene).
TEST_CASE("incremental: light-groups — op not touching Influences/emission reuses prev table") {
    LitScene s = make_lit_scene();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;

    // Pin the non-trivial pre-state: two lights, and a grouping that is NOT just
    // one-singleton-per-light (l0 and l1 share a group via the connected region),
    // so "reuse prev.light_groups" is a meaningful, falsifiable claim.
    REQUIRE(prev.lights.size() == 2);
    REQUIRE(prev.light_groups.size() == 1);            // l0,l1 co-influence one region
    REQUIRE(prev.light_groups[0].size() == 2);
    const std::vector<std::vector<NodeId>> prev_groups = prev.light_groups;

    // The edit: move the sub-tree (no emission / Influences change whatsoever).
    aleph::lowering::Op op =
        aleph::lowering::SetTransform{s.mid, translate(0, 5, 4)};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;

    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    // Byte-identical to full (the umbrella contract still holds).
    CHECK(freeze(*inc) == freeze(*full));

    // The §6.4 reuse claim: NO sheaf recompute, and the table is `prev`'s verbatim.
    CHECK_FALSE(stats.light_groups_recomputed);
    CHECK(inc->light_groups == prev_groups);
    // (Cross-check: it also equals what a full lower derives — reuse is correct.)
    CHECK(inc->light_groups == full->light_groups);
}

// ── (4b) LIGHT GROUPS — an op that DOES touch emission recomputes & == full ───
//
// SPEC §6.4 (the recompute half). A SetMaterial that makes mesh_0's previously-
// dark material EMISSIVE changes the light table's membership (mesh_0 joins it),
// so the lowering conservatively flags the grouping dirty: the sheaf H⁰ pass
// reruns (`light_groups_recomputed == true`) and the returned `light_groups` must
// equal a full `lower(after)`'s. (Topology is unchanged, so the recomputed groups
// happen to match `prev` here too — but the CONTRACT is "matches full", which is
// what we assert.)
TEST_CASE("incremental: light-groups — op touching emission recomputes and == full") {
    LitScene s = make_lit_scene();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;
    REQUIRE(prev.lights.size() == 2);  // l0, l1 only (no emissive meshes yet)

    // The edit: light up mesh_0's material (emit > 0). This toggles mesh_0 INTO
    // the light table, which is exactly the "touches emission" case (SPEC §3.4).
    aleph::lowering::MaterialParams edited{};
    edited.kind   = MaterialKind::Emissive;
    edited.albedo = Vec3{0.8f, 0.2f, 0.2f};
    edited.fuzz   = 0.0f;
    edited.ior    = 1.5f;
    edited.emit   = Vec3{6, 6, 6};  // now emissive -> joins the light table
    aleph::lowering::Op op = aleph::lowering::SetMaterial{s.mesh_0, edited};

    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;

    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    // Byte-identical to full.
    CHECK(freeze(*inc) == freeze(*full));

    // The §6.4 recompute claim: the sheaf H⁰ pass DID rerun, and the resulting
    // groups equal a full lower's (the contract — not necessarily prev's).
    CHECK(stats.light_groups_recomputed);
    CHECK(inc->light_groups == full->light_groups);
    // The emissive mesh actually entered the light table (sanity: the op had teeth).
    CHECK(full->lights.size() == prev.lights.size() + 1);
}

// ── (5) APPLYRULE — incremental == full via fallback, no dangling handles ─────
//
// SPEC §6.5. `ApplyRule` is not yet dirty-set incremental, so `lower_incremental`
// FALLS BACK to a full `lower(after)` — byte-identical by construction. We apply
// the monotone `refine_cell` rule (splits a Mesh into two child meshes sharing its
// material) and assert: (1) incremental == full byte-for-byte; (2) NO DANGLING
// HANDLES — every handle_map key resolves to its own entity, the map size equals
// the entity count, and every key names a live Mesh in the post-graph; (3) the
// fallback stats reflect a full recompute.
TEST_CASE("incremental: ApplyRule == full via fallback, no dangling handles") {
    ManyMesh s = make_many_mesh();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;
    REQUIRE(prev.entities.size() == 3);  // N = 3 meshes pre-rewrite

    // The edit: a monotone DPO refine — splits some matched Mesh into M_a, M_b,
    // both referencing the same Material (created_nodes = 2, deleted = 0).
    aleph::lowering::Op op =
        aleph::lowering::ApplyRule{&aleph::dpo::rules::refine_cell()};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;
    CHECK(rec.created_nodes.size() == 2);  // M_a, M_b
    CHECK(rec.deleted_nodes.empty());      // monotone

    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    // (1) ORACLE: incremental (the fallback to full) == full, byte-for-byte.
    CHECK(freeze(*inc) == freeze(*full));

    // (2) NO DANGLING HANDLES: the patched map is fully consistent against the
    //     post-graph — every key resolves to its own live-Mesh entity, bijectively.
    CHECK(no_dangling_handles(s.g, *inc));
    // The rewrite added geometry, so the entity count grew (the rule had teeth).
    CHECK(inc->entities.size() == prev.entities.size() + 2);

    // (3) FALLBACK stats: ApplyRule is not dirty-set incremental this wave, so the
    //     fallback reports a full recompute (recomputed == |entities|, groups rerun).
    CHECK(stats.recomputed_entities == inc->entities.size());
    CHECK(stats.light_groups_recomputed);
}
