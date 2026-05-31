#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

import aleph.math;
import aleph.types;
import aleph.graph;
import aleph.containers;
import aleph.flow;        // ricci_curvature (the Ollivier-Ricci entry point)
import aleph.dpo;         // RewriteRecord, rules::refine_cell
import aleph.scene;       // Scene + per-primitive importance SoA (build target)
import aleph.lowering;    // entity_importance, lower, lower_incremental, IncrementalStats,
                          // build_render_scene, RenderScene

// Phase 5.x-b — Flow (Ollivier-Ricci) → Adaptive-SPP Importance. SPEC §5
// tests 1 and 6 (Wave W1) plus test 2 (Wave W2). This file PINS the
// `aleph.lowering:importance` contract — the math layer (`aleph.flow`) informing
// the lowering, baked as a plain per-entity importance — and then follows that
// importance one hop further, into the renderer's `Scene` (test 2), WITHOUT the
// renderer ever touching the flow layer.
//
//   (1) importance_matches_ricci (EXACT). On a KNOWN mesh-adjacency graph,
//       `aleph::lowering::entity_importance(g)` equals the deterministic
//       aggregate of `aleph::flow::ricci_curvature(g)` — recomputed here,
//       independently, straight from the flow module (the SPEC §4.1 recipe:
//       per-Mesh MEAN of its incident Adjacent-edge curvatures, then a
//       min-max NORMALIZE across meshes to [0,1]; a mesh with no Adjacent
//       edge → 0; all-equal/no-edges → 0 for all). We cross-check the lowering
//       against the flow module DIRECTLY (not against a frozen golden), so the
//       test proves the lowering is a faithful function of the curvature, and
//       we assert it is DETERMINISTIC across two calls bit-for-bit (SPEC §7:
//       all-f64, insertion order, OrderedMap, no hash-order creep).
//
//   (2) importance_flows_to_scene (SPEC §5.2). The per-entity importance the
//       lowering baked onto `LoweredScene` must survive the SECOND arrow of the
//       pipeline — `build_render_scene` — and land, per entity, on the matching
//       SoA primitive. We lower a MULTI-MESH graph with MIXED geometry (spheres,
//       a quad, a tri), build the `RenderScene`, and for EACH entity look up the
//       per-primitive `Scene` importance via the exact primitive that entity
//       became, asserting it equals `LoweredScene.importance[entity]` (compared
//       as `f32`, since the Scene stores importance as a plain `f32` array per
//       SPEC §4.2). The entity→primitive map is reconstructed by REPLAYING the
//       build's own per-store dispatch (variant → spheres/quads/tris, with a
//       per-store running index), so the test pins the bake INDEPENDENTLY of the
//       build internals rather than trusting an emission order. Mixed geometry
//       proves the bake routes to the right store, not just "the sphere store."
//
//   (6) incremental_importance (SPEC §5.6, the 5.x-c reuse contract extended to
//       importance). An `Op` that does NOT change the `Adjacent`/mesh topology
//       (a `SetTransform`) must REUSE `prev.importance` verbatim — the Ricci
//       pass does not rerun (`stats.importance_recomputed == false`) and
//       `inc.importance == prev.importance`. An `Op` that DOES change the
//       `Adjacent` 1-skeleton (an `ApplyRule{refine_cell}`, which inserts an
//       `Adjacent` edge between two fresh meshes) must RECOMPUTE
//       (`stats.importance_recomputed == true`) and the result must equal a
//       full `lower(after)`'s importance. Incremental is purely an optimization;
//       it may never diverge from full (SPEC §1/§2).
//
// Architectural boundary (SPEC §1): `aleph.lowering` is the SANCTIONED
// cross-cutter that links `aleph.flow`; `aleph.scene`/`render.rt` never do. This
// test lives on the lowering side and is the only place flow and lowering meet.
//
// aleph_flags_isa (no exceptions): `lower`/`apply_op`/`lower_incremental` all
// return `std::expected`; we REQUIRE has_value() before trusting any post-state.

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Mat4;
using aleph::math::Vec3;

namespace {

LocalTransform translate(float x, float y, float z) {
    return LocalTransform{Mat4::translate(Vec3{x, y, z})};
}

// ── a KNOWN mesh-adjacency graph: a 4-mesh PATH joined by Adjacent edges ──────
//
//   root Transform Contains: cam, m0, m1, m2, m3
//   m0 —Adjacent→ m1 —Adjacent→ m2 —Adjacent→ m3      (a path 1-skeleton)
//   each mesh References its own (non-emissive) Material.
//
// A PATH is chosen deliberately: the two endpoints (m0, m3) are degree-1 and the
// two interior meshes (m1, m2) are degree-2, so the Ollivier-Ricci curvatures of
// their incident edges differ — the per-mesh aggregate is NOT constant, which
// makes the min-max NORMALIZATION non-degenerate (a real [0,1] spread, not the
// all-equal → 0 degenerate branch). Every mesh has at least one Adjacent edge,
// so the "no edges → 0" branch never fires and the cross-check is unambiguous.
struct MeshPath {
    Graph  g;
    NodeId root{}, cam{};
    NodeId m0{}, m1{}, m2{}, m3{};
};

NodeId add_mesh(Graph& g, NodeId parent, const char* name, Vec3 center) {
    const NodeId mesh_id = g.alloc_node_id();
    Mesh mesh{mesh_id, std::string(name), 0};
    mesh.geometry = SphereLocal{center, 1.0f};
    g.insert_node(std::move(mesh));

    const NodeId mat_id = g.alloc_node_id();
    Material mat{mat_id, MaterialKind::Lambertian};
    mat.albedo = Vec3{0.7f, 0.7f, 0.7f};
    mat.emit   = Vec3{0, 0, 0};  // non-emissive: keeps the light table out of it
    g.insert_node(std::move(mat));

    (void)g.add_edge(EdgeKind::Contains,   parent,  mesh_id);
    (void)g.add_edge(EdgeKind::References, mesh_id, mat_id);
    return mesh_id;
}

MeshPath make_mesh_path() {
    MeshPath s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    s.cam = g.alloc_node_id();
    Camera cam{};
    cam.id        = s.cam;
    cam.sensor_id = std::string("sensor0");
    cam.look_from = Vec3{0, 0, 12};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 45.0f;
    g.insert_node(std::move(cam));
    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);

    s.m0 = add_mesh(g, s.root, "m0", Vec3{-3, 0, 0});
    s.m1 = add_mesh(g, s.root, "m1", Vec3{-1, 0, 0});
    s.m2 = add_mesh(g, s.root, "m2", Vec3{ 1, 0, 0});
    s.m3 = add_mesh(g, s.root, "m3", Vec3{ 3, 0, 0});

    // The Adjacent 1-skeleton: a path m0 - m1 - m2 - m3.
    (void)g.add_edge(EdgeKind::Adjacent, s.m0, s.m1);
    (void)g.add_edge(EdgeKind::Adjacent, s.m1, s.m2);
    (void)g.add_edge(EdgeKind::Adjacent, s.m2, s.m3);
    return s;
}

// The SPEC §4.1 aggregate, recomputed INDEPENDENTLY straight from the flow
// module so the test cross-checks the lowering against `aleph.flow` directly:
//   * per Mesh: MEAN of the curvatures of its incident Adjacent edges;
//   * a Mesh with NO incident Adjacent edge → 0 (and it does not enter the
//     min/max over the edged meshes);
//   * min-max NORMALIZE the edged meshes' means to [0,1];
//   * all-equal (hi == lo) or no edges at all → 0 for every mesh.
// Returns a map Mesh NodeId → importance in [0,1], over exactly the graph's
// Mesh nodes (in NodeId order — deterministic and independent of hash order).
aleph::containers::OrderedMap<NodeId, double>
reference_importance(const Graph& g) {
    const aleph::flow::RicciMap rc = aleph::flow::ricci_curvature(g);

    // Enumerate Mesh nodes in ascending NodeId order (stable, hash-free).
    std::vector<NodeId> meshes;
    for (auto [id, node] : g.nodes()) {
        if (kind_of(node) == NodeKind::Mesh) meshes.push_back(id);
    }
    std::sort(meshes.begin(), meshes.end());

    // Per-mesh mean of incident Adjacent-edge curvatures; track which have edges.
    std::vector<double> mean(meshes.size(), 0.0);
    std::vector<char>   has_edge(meshes.size(), 0);
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        double      sum = 0.0;
        std::size_t cnt = 0;
        for (auto [key, kappa] : rc) {
            if (key.first == meshes[i] || key.second == meshes[i]) {
                sum += kappa;
                ++cnt;
            }
        }
        if (cnt > 0) {
            mean[i]     = sum / static_cast<double>(cnt);
            has_edge[i] = 1;
        }
    }

    // Min-max range over the EDGED meshes only.
    bool   any = false;
    double lo = 0.0, hi = 0.0;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (!has_edge[i]) continue;
        if (!any) {
            lo = hi = mean[i];
            any = true;
        } else {
            if (mean[i] < lo) lo = mean[i];
            if (mean[i] > hi) hi = mean[i];
        }
    }
    const double span = hi - lo;

    aleph::containers::OrderedMap<NodeId, double> out;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        double imp = 0.0;
        if (has_edge[i] && span > 0.0) {
            imp = (mean[i] - lo) / span;
        }
        out.insert(meshes[i], imp);
    }
    return out;
}

// ── a MIXED-geometry multi-mesh graph for the §5.2 flow-to-Scene test ────────
//
//   root Transform Contains: cam, m0 (Sphere), m1 (Quad), m2 (Tri), m3 (Sphere)
//   m0 —Adjacent→ m1 —Adjacent→ m2 —Adjacent→ m3        (a 4-mesh path)
//   each mesh References its own (non-emissive) Material.
//
// Geometry kinds {Sphere, Quad, Tri, Sphere} so `build_render_scene` must route
// the entities across ALL THREE SoA stores — and with TWO sphere entities the
// per-store running index must advance past 0, proving the bake indexes each
// store independently (not "primitive index == entity index"). A 4-mesh PATH is
// chosen (not 3) deliberately: a 3-path is symmetric, so both its edges share
// one curvature and every per-mesh mean is equal — the degenerate all-zero
// "uniform" branch (SPEC §4.1). The 4-path's endpoints (degree-1) and interiors
// (degree-2) give a NON-degenerate [0,1] spread, so "carries the importance" is
// a real claim the per-primitive Scene arrays must reproduce exactly.
struct MixedMeshes {
    Graph  g;
    NodeId root{}, cam{};
    NodeId m0{}, m1{}, m2{}, m3{};  // Sphere, Quad, Tri, Sphere
};

NodeId add_mesh_geom(Graph& g, NodeId parent, const char* name,
                     aleph::types::GeometryPayload geom) {
    const NodeId mesh_id = g.alloc_node_id();
    Mesh mesh{mesh_id, std::string(name), 0};
    mesh.geometry = std::move(geom);
    g.insert_node(std::move(mesh));

    const NodeId mat_id = g.alloc_node_id();
    Material mat{mat_id, MaterialKind::Lambertian};
    mat.albedo = Vec3{0.6f, 0.6f, 0.6f};
    mat.emit   = Vec3{0, 0, 0};  // non-emissive: no light-table interference
    g.insert_node(std::move(mat));

    (void)g.add_edge(EdgeKind::Contains,   parent,  mesh_id);
    (void)g.add_edge(EdgeKind::References, mesh_id, mat_id);
    return mesh_id;
}

MixedMeshes make_mixed_meshes() {
    MixedMeshes s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    s.cam = g.alloc_node_id();
    Camera cam{};
    cam.id        = s.cam;
    cam.sensor_id = std::string("sensor0");
    cam.look_from = Vec3{0, 0, 12};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 45.0f;
    g.insert_node(std::move(cam));
    (void)g.add_edge(EdgeKind::Contains, s.root, s.cam);

    s.m0 = add_mesh_geom(g, s.root, "m0",
                         aleph::types::SphereLocal{Vec3{-3, 0, 0}, 1.0f});
    s.m1 = add_mesh_geom(g, s.root, "m1",
                         aleph::types::QuadLocal{Vec3{-1, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}});
    s.m2 = add_mesh_geom(g, s.root, "m2",
                         aleph::types::TriLocal{Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{1, 1, 0}});
    s.m3 = add_mesh_geom(g, s.root, "m3",
                         aleph::types::SphereLocal{Vec3{3, 0, 0}, 1.0f});

    // The Adjacent 1-skeleton: a 4-path m0 - m1 - m2 - m3 (non-degenerate κ).
    (void)g.add_edge(EdgeKind::Adjacent, s.m0, s.m1);
    (void)g.add_edge(EdgeKind::Adjacent, s.m1, s.m2);
    (void)g.add_edge(EdgeKind::Adjacent, s.m2, s.m3);
    return s;
}

// Reconstruct, for entity index `i`, the per-primitive `Scene` importance of the
// primitive that entity became — by REPLAYING the build's own per-store dispatch
// (SPEC §4.3 `add_entity`): each entity's `world_geometry` variant selects the
// SoA store (Sphere/Quad/Tri) it is appended to, and within that store entities
// are appended in `entities` order, so the entity's primitive index is the count
// of prior entities of the SAME variant. We then read that store's `importance`
// at that index. Independent of build internals — we only assume the documented
// "one entity → one primitive in the matching store, in order" contract.
aleph::math::f32 scene_importance_for_entity(
    const aleph::scene::Scene& scene,
    const std::vector<aleph::lowering::LoweredEntity>& entities,
    std::size_t i) {
    std::uint32_t sphere_idx = 0, quad_idx = 0, tri_idx = 0;
    for (std::size_t k = 0; k < i; ++k) {
        std::visit([&](const auto& gpy) {
            using G = std::decay_t<decltype(gpy)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>)      ++sphere_idx;
            else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>)   ++quad_idx;
            else                                                            ++tri_idx;
        }, entities[k].world_geometry);
    }
    return std::visit(
        [&](const auto& gpy) -> aleph::math::f32 {
            using G = std::decay_t<decltype(gpy)>;
            if constexpr (std::is_same_v<G, aleph::types::SphereLocal>) {
                REQUIRE(sphere_idx < scene.spheres.importance.size());
                return scene.spheres.importance[sphere_idx];
            } else if constexpr (std::is_same_v<G, aleph::types::QuadLocal>) {
                REQUIRE(quad_idx < scene.quads.importance.size());
                return scene.quads.importance[quad_idx];
            } else {  // aleph::types::TriLocal
                REQUIRE(tri_idx < scene.tris.importance.size());
                return scene.tris.importance[tri_idx];
            }
        },
        entities[i].world_geometry);
}

// Bit-exact f64 equality (SPEC §7 determinism: same bits, not just same value).
bool bits_equal(double a, double b) {
    std::uint64_t ab = 0, bb = 0;
    std::memcpy(&ab, &a, sizeof(ab));
    std::memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

}  // namespace

// ── (1) importance == flow Ricci aggregate, exact; deterministic ─────────────
TEST_CASE("importance: entity_importance equals the flow Ricci aggregate (exact, deterministic)") {
    MeshPath s = make_mesh_path();

    // The lowering's importance, and the independent flow-derived reference.
    const auto got = aleph::lowering::entity_importance(s.g);
    const auto ref = reference_importance(s.g);

    // Teeth: the reference must itself be NON-DEGENERATE — at least two distinct
    // normalized values in [0,1], with a min of 0 and a max of 1 — else min-max
    // normalization is vacuous and "matches the aggregate" proves nothing.
    REQUIRE(ref.size() == 4);  // exactly the four path meshes
    {
        double rmin = 2.0, rmax = -1.0;
        for (auto [id, v] : ref) {
            (void)id;
            if (v < rmin) rmin = v;
            if (v > rmax) rmax = v;
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
        }
        CHECK(rmin == doctest::Approx(0.0));  // min-max anchors at 0
        CHECK(rmax == doctest::Approx(1.0));  // ...and at 1
        CHECK(rmax > rmin);                   // a real spread (non-degenerate)
    }

    // EXACT cross-check: same key set, same f64 bits, for every mesh.
    REQUIRE(got.size() == ref.size());
    for (auto [id, ref_v] : ref) {
        const double* got_v = got.get(id);
        REQUIRE(got_v != nullptr);         // lowering keyed by every Mesh
        CHECK(bits_equal(*got_v, ref_v));  // bit-for-bit == the flow aggregate
    }
    // ...and the lowering introduces no extra keys beyond the graph's meshes.
    for (auto [id, got_v] : got) {
        (void)got_v;
        CHECK(ref.contains(id));
    }

    // DETERMINISM across two calls: byte-identical (same key order, same bits).
    const auto again = aleph::lowering::entity_importance(s.g);
    REQUIRE(again.size() == got.size());
    auto it_a = got.begin();
    auto it_b = again.begin();
    for (; it_a != got.end() && it_b != again.end(); ++it_a, ++it_b) {
        CHECK((*it_a).first == (*it_b).first);              // same key, same order
        CHECK(bits_equal((*it_a).second, (*it_b).second));  // same f64 bits
    }
    CHECK(it_a == got.end());
    CHECK(it_b == again.end());
}

// ── (2) importance flows through build_render_scene to per-primitive Scene ───
//
// The lowering bakes per-entity importance onto `LoweredScene.importance`
// (aligned to `entities`); `build_render_scene` must carry it one hop further —
// onto the matching SoA primitive in the renderer's `Scene` (SPEC §3/§4.2). On a
// MULTI-MESH, MIXED-geometry graph we lower, build, and check that for EVERY
// entity the per-primitive `Scene` importance (looked up via the primitive that
// entity became, by replaying the build's per-store dispatch) equals
// `LoweredScene.importance[entity]`, compared as `f32` (the Scene stores it as a
// plain `f32` array — `aleph.scene` never sees `aleph.flow`). Mixed geometry
// forces the bake to route to three different stores; a non-degenerate spread
// makes "carries the importance" a real claim, not a vacuous all-zero pass.
TEST_CASE("importance: build_render_scene carries per-entity importance onto the matching Scene primitive") {
    MixedMeshes s = make_mixed_meshes();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    // Pin the IR the build consumes: four entities (sphere/quad/tri/sphere), and
    // an importance vector aligned to them with a REAL spread (non-degenerate, so
    // a correct carry is meaningful — an all-zero array would pass vacuously).
    REQUIRE(ls.entities.size() == 4);
    REQUIRE(ls.importance.size() == ls.entities.size());
    {
        double imin = ls.importance[0], imax = ls.importance[0];
        for (double v : ls.importance) {
            if (v < imin) imin = v;
            if (v > imax) imax = v;
            CHECK(v >= 0.0);
            CHECK(v <= 1.0);
        }
        CHECK(imax > imin);  // a real importance spread to carry into the Scene
    }

    // The second arrow: LoweredScene -> RenderScene (SoA + BVH).
    const aleph::lowering::RenderScene rs = aleph::lowering::build_render_scene(ls);
    const aleph::scene::Scene& scene = rs.scene;

    // The mixed geometry must have produced two sphere primitives plus one quad
    // and one tri, so the per-store importance arrays are sized to match (the
    // parallel-array invariant the bake must maintain) and the sphere store has
    // two entries — exercising a per-store index past 0.
    REQUIRE(scene.spheres.importance.size() == 2);
    REQUIRE(scene.quads.importance.size()   == 1);
    REQUIRE(scene.tris.importance.size()    == 1);

    // The core claim: for EACH entity, the per-primitive Scene importance — read
    // via the exact primitive that entity became — equals the IR's importance for
    // that entity, as f32. We compare bit-for-bit on the f32 cast so this is also
    // a determinism pin (no value drift, SPEC §7), not merely approximate.
    for (std::size_t i = 0; i < ls.entities.size(); ++i) {
        const aleph::math::f32 scene_imp =
            scene_importance_for_entity(scene, ls.entities, i);
        const aleph::math::f32 ir_imp =
            static_cast<aleph::math::f32>(ls.importance[i]);
        CHECK(scene_imp == ir_imp);
    }

    // Cross-check the carry against the flow module DIRECTLY (the bake is correct,
    // not just self-consistent with the IR): re-key the independent Ricci
    // aggregate onto each entity's `source` and confirm the Scene primitive holds
    // it (as f32). This ties the per-primitive Scene importance all the way back
    // to `aleph.flow::ricci_curvature`, through the lowering, without the renderer
    // ever importing flow.
    const auto ref = reference_importance(s.g);
    for (std::size_t i = 0; i < ls.entities.size(); ++i) {
        const NodeId src = ls.entities[i].source;
        const double* ref_v = ref.get(src);
        REQUIRE(ref_v != nullptr);
        const aleph::math::f32 scene_imp =
            scene_importance_for_entity(scene, ls.entities, i);
        CHECK(scene_imp == static_cast<aleph::math::f32>(*ref_v));
    }
}

// ── (6a) an Op NOT changing Adjacent topology reuses prev.importance ─────────
//
// A `SetTransform` shifts world geometry but touches NO `Adjacent` edge and no
// Mesh node — the 1-skeleton is unchanged, so the Ricci pass need not rerun. The
// lowering must REUSE `prev.importance` verbatim and report it did no work
// (`stats.importance_recomputed == false`). Incremental must still equal a full
// re-lower's importance (reuse is correct, not merely cheap).
TEST_CASE("importance: incremental — Op not changing Adjacent topology reuses prev.importance") {
    MeshPath s = make_mesh_path();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;

    // Pin a non-trivial pre-state: four entities, and an importance vector that
    // is NOT all-equal (so "reuse" is a meaningful claim against a real spread).
    REQUIRE(prev.entities.size() == 4);
    REQUIRE(prev.importance.size() == prev.entities.size());
    {
        double pmin = prev.importance[0], pmax = prev.importance[0];
        for (double v : prev.importance) {
            if (v < pmin) pmin = v;
            if (v > pmax) pmax = v;
        }
        CHECK(pmax > pmin);  // a real importance spread to preserve
    }
    const std::vector<double> prev_importance = prev.importance;

    // The edit: move the root subtree. No Adjacent edge / Mesh node is touched.
    aleph::lowering::Op op =
        aleph::lowering::SetTransform{s.root, translate(0, 0, 1)};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;

    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    // The reuse claim: NO Ricci recompute, and the vector is `prev`'s verbatim.
    CHECK_FALSE(stats.importance_recomputed);
    REQUIRE(inc->importance.size() == prev_importance.size());
    for (std::size_t i = 0; i < prev_importance.size(); ++i) {
        CHECK(bits_equal(inc->importance[i], prev_importance[i]));
    }
    // Cross-check: reuse is also CORRECT — it equals what a full lower derives.
    REQUIRE(full->importance.size() == inc->importance.size());
    for (std::size_t i = 0; i < inc->importance.size(); ++i) {
        CHECK(bits_equal(inc->importance[i], full->importance[i]));
    }
}

// ── (6b) an Op that DOES change Adjacent topology recomputes and == full ─────
//
// `ApplyRule{refine_cell}` splits a matched Mesh into two child meshes joined by
// a NEW `Adjacent` edge (R adds `adjacent(M_a, M_b)`), so the 1-skeleton — and
// hence the Ricci curvature and the per-entity importance — genuinely change.
// The lowering must RECOMPUTE (`stats.importance_recomputed == true`) and the
// recomputed importance must equal a full `lower(after)`'s, entity-for-entity.
TEST_CASE("importance: incremental — Op changing Adjacent topology recomputes and == full") {
    MeshPath s = make_mesh_path();

    auto before = aleph::lowering::lower(s.g);
    REQUIRE(before.has_value());
    const aleph::lowering::LoweredScene& prev = *before;
    REQUIRE(prev.entities.size() == 4);

    // The edit: a monotone refine — adds M_a, M_b and an Adjacent edge between
    // them, changing the Adjacent 1-skeleton (created_nodes = 2, deleted = 0).
    aleph::lowering::Op op =
        aleph::lowering::ApplyRule{&aleph::dpo::rules::refine_cell()};
    auto applied = aleph::lowering::apply_op(s.g, op);
    REQUIRE(applied.has_value());
    const aleph::dpo::RewriteRecord& rec = *applied;
    CHECK(rec.created_nodes.size() == 2);  // M_a, M_b — the rule had teeth
    CHECK(rec.deleted_nodes.empty());      // monotone

    auto full = aleph::lowering::lower(s.g);
    REQUIRE(full.has_value());

    aleph::lowering::IncrementalStats stats{};
    auto inc = aleph::lowering::lower_incremental(prev, s.g, op, rec, &stats);
    REQUIRE(inc.has_value());

    // The recompute claim: the Ricci pass DID rerun, and the result equals full.
    CHECK(stats.importance_recomputed);
    REQUIRE(inc->importance.size() == full->importance.size());
    REQUIRE(inc->importance.size() == inc->entities.size());  // aligned to entities
    for (std::size_t i = 0; i < inc->importance.size(); ++i) {
        CHECK(bits_equal(inc->importance[i], full->importance[i]));
    }
    // The recomputed importance must ALSO match the independent flow aggregate,
    // re-keyed onto the post-graph's entities (the recompute is correct, not just
    // self-consistent with `full`). entity_importance is keyed by Mesh NodeId;
    // map it through each entity's `source`.
    const auto ref = reference_importance(s.g);
    for (std::size_t i = 0; i < inc->entities.size(); ++i) {
        const NodeId src = inc->entities[i].source;
        const double* ref_v = ref.get(src);
        REQUIRE(ref_v != nullptr);
        CHECK(bits_equal(inc->importance[i], *ref_v));
    }
}
