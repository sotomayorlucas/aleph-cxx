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
import aleph.scene;
import aleph.dpo;
import aleph.lowering;

#include "lowering_freeze.hpp"  // padding-proof, leaf-wise byte serializers

// SPEC §8.7 — dpo_edit (anti-dangling). The STRUCTURAL leg of the return path.
//
//   editor gesture ──▶ Op ──▶ apply_op ──▶ TRANSACTIONAL graph rewrite ──▶ re-lower
//
// Where §8.5 (edit_material) exercises the ATTRIBUTE op family (in-place, no
// node created/deleted), this test exercises the STRUCTURAL family (SPEC §5):
// ops that create/delete nodes+edges, applied via `aleph.dpo` and therefore
// ALL-OR-NOTHING — a new VALID state or NO partial effect. The two structural
// gestures under test are the ones §8.7 names explicitly:
//
//   * apply_op(DeleteObject)  — remove a Mesh (and cascade its incident edges).
//   * apply_op(ApplyRule)     — replay a DPO Rule (here a monotone "refine"
//                               split that ADDS geometry) onto the graph.
//
// Oracle (SPEC §8.7), for each successful structural op:
//   (a) re-lowering succeeds -> a VALID Scene (lower() returns a value, not a
//       LowerError; broken references would surface here, never silently);
//   (b) NO DANGLING HANDLES: every `handle_map` key is a live graph node, every
//       value indexes a real `entities` slot, and `build_render_scene` over the
//       re-lowered IR yields a Scene whose primitive/material/light counts agree
//       with the IR (the build is a total translation, so a dangling handle would
//       desync the counts);
//   (c) `validate_all` holds on the post-edit GRAPH (the single source of truth);
//   (d) SURVIVOR STABILITY: a node that outlived the edit keeps its identity in
//       the re-lowered IR (its `source` is still mapped) — handles are stable for
//       survivors, exactly as the determinism contract (SPEC §7) demands.
//
// And the rollback half of §8.7: "a rolled-back op leaves graph + lowering
// unchanged." A structural op that cannot produce a valid post-state (or whose
// target is not the kind it edits) must fail WITHOUT touching the live graph —
// so re-lowering after the failed op is BYTE-IDENTICAL to lowering before it.
//
// No exceptions (aleph_flags_isa): apply_op and lower both return std::expected;
// we REQUIRE has_value() before trusting any post-state, and assert the error
// discriminant on the rollback path.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

// ── canonical enriched graph (lowerable) ─────────────────────────────────────
// root Transform (identity) Contains: a Camera, ONE Mesh (unit sphere, red
// Lambertian via References), and an explicit Light. Both the mesh's material
// and the absence of emission keep the light table = { the Light node } so the
// structural edits below have an unambiguous before/after to diff against.
//
// We keep exactly one Mesh so that DeleteObject's effect is crisp (1 entity ->
// 0), and so the only References→Material edge in the graph belongs to that mesh
// — letting us also build a clean *dangling-reference* variant for a rollback.
struct Fixture {
    Graph  g;
    NodeId root{}, cam{}, mesh{}, mat{}, light{};
};

Fixture make_fixture() {
    Fixture s;
    Graph& g = s.g;

    // root Transform at identity (no incoming Contains -> a root).
    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    // Camera with a concrete pose (CameraExclusive: exactly one).
    s.cam = g.alloc_node_id();
    Camera cam{s.cam, std::string("sensor0")};
    cam.look_from = Vec3{0, 0, 5};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 40.0f;
    g.insert_node(std::move(cam));

    // The lone Mesh — a unit sphere at the origin.
    s.mesh = g.alloc_node_id();
    Mesh mesh{s.mesh, std::string("sphere"), 0};
    mesh.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(mesh));

    // Red Lambertian, NOT emissive -> stays out of the light table.
    s.mat = g.alloc_node_id();
    Material mat{s.mat, MaterialKind::Lambertian};
    mat.albedo = Vec3{1.0f, 0.0f, 0.0f};
    mat.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mat));

    // Explicit Light node — the lone light-table member.
    s.light = g.alloc_node_id();
    Light light{s.light, LightKind::Area, std::string("emit0")};
    light.emission = Vec3{4, 4, 4};
    light.geometry = QuadLocal{Vec3{-1, 2, -1}, Vec3{2, 0, 0}, Vec3{0, 0, 2}};
    g.insert_node(std::move(light));

    // Hierarchy.
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.cam);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.mesh);
    (void)g.add_edge(EdgeKind::Contains,   s.root, s.light);
    // The mesh references its material (must resolve -> no DanglingReference).
    (void)g.add_edge(EdgeKind::References, s.mesh, s.mat);
    return s;
}

// ── handle / IR consistency oracle (the heart of "no dangling handles") ──────
// A LoweredScene's handle_map is the NodeId -> entities-index map. "No dangling
// handles" means, against the live graph `g` it was lowered from:
//   * every key is a live graph node (the source still exists),
//   * every value is in-range for `entities`,
//   * and that entities slot is sourced from exactly that key (the map and the
//     vector agree),
//   * and every entity is reachable through the map (the map covers entities 1:1).
// This is precisely what a structural rewrite could break by leaving an entity
// keyed to a node the rewrite deleted, or by shifting indices without re-keying.
[[nodiscard]] bool handles_consistent(const Graph& g,
                                      const aleph::lowering::LoweredScene& ls) {
    // size: one map slot per entity (mesh). Lights that are standalone Light
    // nodes are NOT entities and carry no handle (SPEC §4.1 / §4.3).
    if (ls.handle_map.size() != ls.entities.size()) return false;

    std::vector<bool> covered(ls.entities.size(), false);
    for (auto [nid, idx] : ls.handle_map) {
        // key must be a live graph node...
        if (g.node(nid) == nullptr) return false;
        // ...value must index a real entities slot...
        if (idx >= ls.entities.size()) return false;
        // ...and that slot must be sourced from this very key.
        if (ls.entities[idx].source != nid) return false;
        covered[idx] = true;
    }
    // every entity reachable through the map (1:1, no orphaned slot).
    for (bool c : covered) if (!c) return false;
    // belt-and-braces: each entity's source resolves back through the map.
    for (const auto& e : ls.entities) {
        const std::uint32_t* idx = ls.handle_map.get(e.source);
        if (idx == nullptr) return false;
        if (&ls.entities[*idx] != &e) return false;
    }
    return true;
}

// ── byte-image serializers (the rollback "unchanged" oracle) ─────────────────
// Provided by lowering_freeze.hpp. LoweredScene holds a move-only OrderedMap, so
// it is neither copyable nor trivially comparable. We freeze a lowering into a
// flat byte image (scalar leaves in IR iteration order) and compare images with
// ==, the same technique test_lower_minimal / test_edit_material use — which also
// pins insertion order and f32 bit-patterns. Leaves are walked explicitly (not
// memcpy'd) so the alignas(16) padding inside Vec3-bearing structs never
// pollutes the compare.
using aleph_test_freeze::freeze;

// Count drawable primitives the RenderScene materialized. Used to confirm the
// build is a total translation of the IR — a dangling handle would desync this.
std::size_t scene_primitives(const aleph::scene::Scene& sc) {
    return sc.spheres.r.size() + sc.quads.Qx.size() + sc.tris.v0x.size();
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// (1) apply_op(DeleteObject) — remove the lone Mesh, then re-lower.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: apply_op(DeleteObject) re-lowers to a valid Scene, no dangling handles") {
    Fixture s = make_fixture();

    // Pre-state: one entity, one light, the handle map is consistent.
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    REQUIRE(before->entities.size() == 1);
    REQUIRE(before->lights.size() == 1);
    REQUIRE(handles_consistent(s.g, *before));

    // The structural op: delete the Mesh. Transactional via aleph.dpo — it
    // cascades the mesh's incident edges (its Contains-from-root and its
    // References→Material), so the post-graph has no dangling edge.
    aleph::lowering::Op op = aleph::lowering::DeleteObject{s.mesh};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // (c) the post-edit GRAPH satisfies every invariant (the single truth holds).
    //     The mesh is gone, and the CASCADE also reclaimed its exclusively-
    //     referenced Material (no other mesh References it — leaving it would
    //     leak a node per delete). The root Transform is NEVER cascaded: it
    //     still Contains the camera + light, and it has no Contains parent (the
    //     non-root guard). (max_in_degree = SIZE_MAX: not degree-gated.)
    CHECK(aleph::graph::validate_all(s.g, static_cast<std::size_t>(-1)).has_value());
    CHECK(s.g.node(s.mesh) == nullptr);  // the mesh is actually gone
    CHECK(s.g.node(s.mat) == nullptr);   // exclusive Material cascaded away
    CHECK(s.g.node(s.root) != nullptr);  // root Transform survives (non-root guard)

    // (a) re-lowering succeeds -> a VALID Scene (not a LowerError).
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const aleph::lowering::LoweredScene& ls = *after;

    // The deleted mesh dropped one entity; the standalone Light still lights.
    CHECK(ls.entities.size() == 0);
    CHECK(ls.lights.size() == 1);

    // (b) NO DANGLING HANDLES: the (now empty) handle map is consistent, and the
    //     built RenderScene is a total translation of the IR (light still present;
    //     no orphaned geometry from the removed mesh).
    CHECK(handles_consistent(s.g, ls));
    CHECK(ls.handle_map.get(s.mesh) == nullptr);  // no stale handle to the gone mesh

    const aleph::lowering::RenderScene rs = aleph::lowering::build_render_scene(ls);
    // entities (0) + standalone Light (the quad) = exactly 1 primitive.
    CHECK(scene_primitives(rs.scene) == 1);
    CHECK(rs.scene.lights.size() == 1);
    CHECK(rs.scene.emis.emit.size() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// (2) apply_op(ApplyRule) — a monotone DPO "refine" split that ADDS geometry.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: apply_op(ApplyRule refine_cell) adds geometry; re-lower valid, handles stable") {
    Fixture s = make_fixture();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    REQUIRE(before->entities.size() == 1);
    REQUIRE(handles_consistent(s.g, *before));

    // refine_cell (aleph.dpo::rules): L = { M:mesh -References-> mat:material },
    // R adds two child meshes M_a, M_b each referencing `mat`, joined by Adjacent.
    // It is MONOTONE (no deletions): M and its reference survive, so the original
    // mesh's handle must stay stable while two new entities appear.
    aleph::lowering::Op op =
        aleph::lowering::ApplyRule{&aleph::dpo::rules::refine_cell()};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());

    // The rewrite report (a dpo::RewriteRecord, the shared return-path vocab)
    // reflects the monotone split: two meshes created, nothing deleted.
    const aleph::dpo::RewriteRecord& rec = *applied;
    CHECK(rec.created_nodes.size() == 2);   // M_a, M_b
    CHECK(rec.deleted_nodes.empty());       // monotone: no deletions

    // (c) the post-graph is valid: each new mesh References exactly one Material
    //     (MaterialReferenced), the Adjacent edge is Mesh->Mesh, CameraExclusive
    //     unchanged. validate_all must hold.
    CHECK(aleph::graph::validate_all(s.g, static_cast<std::size_t>(-1)).has_value());

    // (a) re-lower -> a VALID Scene with the two extra entities.
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const aleph::lowering::LoweredScene& ls = *after;

    CHECK(ls.entities.size() == 3);  // original mesh + M_a + M_b
    CHECK(ls.lights.size() == 1);    // the Light node, untouched

    // (d) SURVIVOR STABILITY: the original mesh kept its identity — its source is
    //     still mapped (a stable handle), and resolves the same red Lambertian.
    const std::uint32_t* keep = ls.handle_map.get(s.mesh);
    REQUIRE(keep != nullptr);
    CHECK(ls.entities[*keep].source == s.mesh);
    CHECK(ls.entities[*keep].material.kind == MaterialKind::Lambertian);
    CHECK(ls.entities[*keep].material.albedo == Vec3{1.0f, 0.0f, 0.0f});

    // (b) NO DANGLING HANDLES: the (now 3-slot) map is fully consistent against
    //     the post-graph, and the built RenderScene materializes every entity +
    //     the standalone light — counts agree with the IR (total translation).
    CHECK(handles_consistent(s.g, ls));

    const aleph::lowering::RenderScene rs = aleph::lowering::build_render_scene(ls);
    // 3 entity spheres + 1 standalone light quad = 4 primitives.
    CHECK(scene_primitives(rs.scene) == 4);
    CHECK(rs.scene.spheres.r.size() == 3);
    CHECK(rs.scene.lights.size() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// (3) Rollback: a failed structural op leaves the GRAPH + the LOWERING unchanged.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: a rolled-back DeleteObject leaves graph + lowering byte-identical") {
    Fixture s = make_fixture();

    // Freeze the pre-edit lowering AND the pre-edit graph shape.
    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const std::vector<std::byte> img_before = freeze(*before);
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    // A structural op whose target is the WRONG KIND for DeleteObject (a Material,
    // not a Mesh). DeleteObject names objects (meshes); validating the request
    // against the LIVE graph read-only rejects it BEFORE any snapshot is built, so
    // the live graph is untouched — the all-or-nothing guarantee (SPEC §5). This
    // is the deterministic "rolled-back op" the SPEC pins: no partial effect.
    aleph::lowering::Op bad = aleph::lowering::DeleteObject{s.mat};
    auto applied = aleph::lowering::apply_op(s.g, bad);

    // It failed with a STRUCTURED error (never a silent no-op / silent default).
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::KindMismatch);

    // The GRAPH is unchanged (counts pinned; the target Material still present).
    CHECK(s.g.node_count() == nodes_before);
    CHECK(s.g.edge_count() == edges_before);
    REQUIRE(s.g.node(s.mat) != nullptr);
    CHECK(s.g.node(s.mesh) != nullptr);

    // And re-lowering is BYTE-IDENTICAL to the pre-edit lowering: the LOWERING is
    // unchanged because the truth it derives from never moved.
    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const std::vector<std::byte> img_after = freeze(*after);
    REQUIRE(img_after.size() == img_before.size());
    CHECK(img_after == img_before);

    // Sanity: handles still consistent after the no-op edit.
    CHECK(handles_consistent(s.g, *after));
}

// ─────────────────────────────────────────────────────────────────────────────
// (4) Rollback via a non-existent target: same all-or-nothing guarantee.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: a DeleteObject on a missing node is a no-op (graph + lowering unchanged)") {
    Fixture s = make_fixture();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const std::vector<std::byte> img_before = freeze(*before);
    const std::size_t nodes_before = s.g.node_count();
    const std::size_t edges_before = s.g.edge_count();

    // A NodeId the graph never allocated. apply_op validates existence against the
    // live graph first, so it fails with NodeNotFound and mutates nothing.
    aleph::lowering::Op bad = aleph::lowering::DeleteObject{NodeId{99999u}};
    auto applied = aleph::lowering::apply_op(s.g, bad);

    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error() == aleph::lowering::OpError::NodeNotFound);

    CHECK(s.g.node_count() == nodes_before);
    CHECK(s.g.edge_count() == edges_before);

    auto after = aleph::lowering::lower(s.g);
    REQUIRE(after.has_value());
    const std::vector<std::byte> img_after = freeze(*after);
    REQUIRE(img_after.size() == img_before.size());
    CHECK(img_after == img_before);
}

// ─────────────────────────────────────────────────────────────────────────────
// (5) DeleteObject cascade: a SHARED Material survives; the per-object Transform
//     (now childless) does not.
// ─────────────────────────────────────────────────────────────────────────────
// Two meshes, EACH under its own per-object Transform (the AddObject topology),
// both Referencing ONE shared Material. Deleting mesh1 must reclaim mesh1's
// dedicated Transform but spare the Material (mesh2 still References it) and
// everything of mesh2's.
TEST_CASE("lowering: DeleteObject cascade spares a shared Material, reclaims the per-object Transform") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 0, 5};
    c.look_at   = Vec3{0, 0, 0};
    c.up        = Vec3{0, 1, 0};
    c.vfov_deg  = 40.0f;
    g.insert_node(std::move(c));

    const NodeId xf1 = g.alloc_node_id();
    g.insert_node(Transform{xf1, 1, LocalTransform{aleph::math::Mat4::identity()}});
    const NodeId mesh1 = g.alloc_node_id();
    Mesh m1{mesh1, std::string("s1"), 0};
    m1.geometry = SphereLocal{Vec3{-2, 0, 0}, 1.0f};
    g.insert_node(std::move(m1));

    const NodeId xf2 = g.alloc_node_id();
    g.insert_node(Transform{xf2, 2, LocalTransform{aleph::math::Mat4::identity()}});
    const NodeId mesh2 = g.alloc_node_id();
    Mesh m2{mesh2, std::string("s2"), 0};
    m2.geometry = SphereLocal{Vec3{2, 0, 0}, 1.0f};
    g.insert_node(std::move(m2));

    // ONE Material, shared by both meshes.
    const NodeId mat = g.alloc_node_id();
    Material shared{mat, MaterialKind::Lambertian};
    shared.albedo = Vec3{0.5f, 0.5f, 0.5f};
    shared.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(shared));

    (void)g.add_edge(EdgeKind::Contains,   root,  cam);
    (void)g.add_edge(EdgeKind::Contains,   root,  xf1);
    (void)g.add_edge(EdgeKind::Contains,   xf1,   mesh1);
    (void)g.add_edge(EdgeKind::Contains,   root,  xf2);
    (void)g.add_edge(EdgeKind::Contains,   xf2,   mesh2);
    (void)g.add_edge(EdgeKind::References, mesh1, mat);
    (void)g.add_edge(EdgeKind::References, mesh2, mat);

    aleph::lowering::Op op = aleph::lowering::DeleteObject{mesh1};
    auto applied = aleph::lowering::apply_op(g, op);
    REQUIRE(applied.has_value());

    CHECK(g.node(mesh1) == nullptr);  // the target mesh is gone
    CHECK(g.node(xf1)   == nullptr);  // its childless dedicated Transform cascaded
    CHECK(g.node(mat)   != nullptr);  // the SHARED Material survives
    CHECK(g.node(mesh2) != nullptr);  // the sibling object is untouched...
    CHECK(g.node(xf2)   != nullptr);  // ...including its own Transform
    CHECK(g.node(root)  != nullptr);

    // mesh2's References edge to the shared Material is intact.
    bool ref2_intact = false;
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::References && e.src == mesh2 && e.dst == mat) {
            ref2_intact = true;
        }
    }
    CHECK(ref2_intact);

    CHECK(aleph::graph::validate_all(g, static_cast<std::size_t>(-1)).has_value());
    auto after = aleph::lowering::lower(g);
    REQUIRE(after.has_value());
    CHECK(after->entities.size() == 1);  // exactly the surviving mesh2
}

// ─────────────────────────────────────────────────────────────────────────────
// (6) DeleteObject cascade: a MULTI-CHILD Transform survives (a sibling mesh
//     keeps it alive); only the mesh + its exclusive Material go.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: DeleteObject spares a multi-child Transform; sibling intact") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 0, 5};
    c.look_at   = Vec3{0, 0, 0};
    c.up        = Vec3{0, 1, 0};
    c.vfov_deg  = 40.0f;
    g.insert_node(std::move(c));

    // ONE Transform with TWO mesh children (each with its own Material).
    const NodeId xf = g.alloc_node_id();
    g.insert_node(Transform{xf, 1, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId mesh_a = g.alloc_node_id();
    Mesh ma{mesh_a, std::string("a"), 0};
    ma.geometry = SphereLocal{Vec3{-1, 0, 0}, 0.5f};
    g.insert_node(std::move(ma));
    const NodeId mat_a = g.alloc_node_id();
    Material mta{mat_a, MaterialKind::Lambertian};
    mta.albedo = Vec3{1, 0, 0};
    mta.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mta));

    const NodeId mesh_b = g.alloc_node_id();
    Mesh mb{mesh_b, std::string("b"), 0};
    mb.geometry = SphereLocal{Vec3{1, 0, 0}, 0.5f};
    g.insert_node(std::move(mb));
    const NodeId mat_b = g.alloc_node_id();
    Material mtb{mat_b, MaterialKind::Lambertian};
    mtb.albedo = Vec3{0, 1, 0};
    mtb.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mtb));

    (void)g.add_edge(EdgeKind::Contains,   root,   cam);
    (void)g.add_edge(EdgeKind::Contains,   root,   xf);
    (void)g.add_edge(EdgeKind::Contains,   xf,     mesh_a);
    (void)g.add_edge(EdgeKind::Contains,   xf,     mesh_b);
    (void)g.add_edge(EdgeKind::References, mesh_a, mat_a);
    (void)g.add_edge(EdgeKind::References, mesh_b, mat_b);

    aleph::lowering::Op op = aleph::lowering::DeleteObject{mesh_a};
    auto applied = aleph::lowering::apply_op(g, op);
    REQUIRE(applied.has_value());

    CHECK(g.node(mesh_a) == nullptr);  // the target mesh is gone
    CHECK(g.node(mat_a)  == nullptr);  // its exclusive Material cascaded
    CHECK(g.node(xf)     != nullptr);  // the Transform SURVIVES: mesh_b keeps it
    CHECK(g.node(mesh_b) != nullptr);
    CHECK(g.node(mat_b)  != nullptr);

    // mesh_b is still wired: xf —Contains→ mesh_b and mesh_b —References→ mat_b.
    bool contains_b = false, refs_b = false;
    for (auto [eid, e] : g.edges()) {
        (void)eid;
        if (e.kind == EdgeKind::Contains   && e.src == xf     && e.dst == mesh_b) contains_b = true;
        if (e.kind == EdgeKind::References && e.src == mesh_b && e.dst == mat_b)  refs_b = true;
    }
    CHECK(contains_b);
    CHECK(refs_b);

    CHECK(aleph::graph::validate_all(g, static_cast<std::size_t>(-1)).has_value());
    auto after = aleph::lowering::lower(g);
    REQUIRE(after.has_value());
    CHECK(after->entities.size() == 1);  // only mesh_b lowers
}

// ─────────────────────────────────────────────────────────────────────────────
// (7) RewriteRecord completeness: a cascading delete reports the WHOLE delete-set
//     ({mesh, transform, material}) in deleted_nodes, and deleted_edges covers
//     EVERY pre-state edge incident to any of them — nothing more, nothing less.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("lowering: cascading DeleteObject reports the full delete-set in the RewriteRecord") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 0, 5};
    c.look_at   = Vec3{0, 0, 0};
    c.up        = Vec3{0, 1, 0};
    c.vfov_deg  = 40.0f;
    g.insert_node(std::move(c));

    // The canonical AddObject footprint: root → xf → mesh → mat (exclusive).
    const NodeId xf = g.alloc_node_id();
    g.insert_node(Transform{xf, 1, LocalTransform{aleph::math::Mat4::identity()}});
    const NodeId mesh = g.alloc_node_id();
    Mesh m{mesh, std::string("s"), 0};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));
    const NodeId mat = g.alloc_node_id();
    Material mt{mat, MaterialKind::Lambertian};
    mt.albedo = Vec3{1, 0, 0};
    mt.emit   = Vec3{0, 0, 0};
    g.insert_node(std::move(mt));

    (void)g.add_edge(EdgeKind::Contains, root, cam);    // survivor edge
    (void)g.add_edge(EdgeKind::Contains, root, xf);     // incident to xf   -> deleted
    (void)g.add_edge(EdgeKind::Contains, xf,   mesh);   // incident to both -> deleted
    (void)g.add_edge(EdgeKind::References, mesh, mat);  // incident to both -> deleted

    // Pre-compute the EXPECTED deleted-edge id set: every pre-state edge incident
    // to any member of the delete-set {mesh, xf, mat}.
    std::vector<EdgeId> expected_edges;
    for (auto [eid, e] : g.edges()) {
        const bool incident = e.src == mesh || e.dst == mesh
                           || e.src == xf   || e.dst == xf
                           || e.src == mat  || e.dst == mat;
        if (incident) expected_edges.push_back(eid);
    }
    REQUIRE(expected_edges.size() == 3);  // root→xf, xf→mesh, mesh→mat

    aleph::lowering::Op op = aleph::lowering::DeleteObject{mesh};
    auto applied = aleph::lowering::apply_op(g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;

    // deleted_nodes == exactly {mesh, xf, mat} (as a SET — order not pinned).
    REQUIRE(rec.deleted_nodes.size() == 3);
    auto deleted_node = [&](NodeId id) {
        for (NodeId d : rec.deleted_nodes) if (d == id) return true;
        return false;
    };
    CHECK(deleted_node(mesh));
    CHECK(deleted_node(xf));
    CHECK(deleted_node(mat));

    // deleted_edges == exactly the pre-computed incident set (as a SET).
    REQUIRE(rec.deleted_edges.size() == expected_edges.size());
    auto deleted_edge = [&](EdgeId id) {
        for (EdgeId d : rec.deleted_edges) if (d == id) return true;
        return false;
    };
    for (EdgeId id : expected_edges) CHECK(deleted_edge(id));

    // Nothing was created, and the survivors (root, cam + their edge) are intact.
    CHECK(rec.created_nodes.empty());
    CHECK(rec.created_edges.empty());
    CHECK(g.node_count() == 2);
    CHECK(g.edge_count() == 1);
    CHECK(aleph::graph::validate_all(g, static_cast<std::size_t>(-1)).has_value());
}
