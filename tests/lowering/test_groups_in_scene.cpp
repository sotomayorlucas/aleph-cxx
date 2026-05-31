#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import aleph.lowering;  // lower, build_render_scene, LoweredScene, RenderScene, light_groups_of
import aleph.scene;     // Scene, Handle32
import aleph.graph;     // Graph
import aleph.types;     // NodeId / Mesh / Light / Material / Camera / Transform / EdgeKind ...
import aleph.math;      // Vec3, Mat4

// Phase 5.x-a, Wave 2 — SPEC §5 test 3 (`lowered_carries_groups`, scene half).
//
// The grouping computed in the lowering must SURVIVE the build hop into the
// renderer's `Scene`:
//
//   Graph ──lower──▶ LoweredScene.light_groups : vector<vector<NodeId>>
//                       │  build_render_scene()
//                       ▼
//                    Scene.light_groups : vector<vector<Handle32>>
//
// This TU exercises the second arrow. We lower a MULTI-LIGHT graph, build the
// RenderScene, and assert that `Scene.light_groups` PARTITIONS `s.lights`
// CONSISTENTLY with `LoweredScene.light_groups`:
//
//   * SAME GROUPING — same number of groups, and each group has the same size
//     as the corresponding `LoweredScene` group (group order is insertion order
//     and preserved through the build, so the i-th scene group corresponds to
//     the i-th lowered group);
//   * CORRECT NodeId -> Handle32 MAPPING — every `NodeId` in a lowered group maps
//     to the `Handle32` the renderer registered for that light in `s.lights`, and
//     the scene groups partition EXACTLY the handles in `s.lights` (a partition:
//     no handle dropped, none duplicated, none invented).
//
// Oracle (SPEC §5.3): `LoweredScene.light_groups` itself — the NodeId partition
// `lower()` baked from the VisibilitySheaf H⁰ components (== `light_groups_of`,
// pinned by test_light_grouping). We do NOT re-derive the sheaf here; the
// architectural boundary (SPEC §1, §4.2) is that the grouping reaches the
// renderer ONLY through the lowering, so the lowered IR is the contract the
// scene must reproduce.
//
// The NodeId -> Handle32 bridge: `build_render_scene` registers each STANDALONE
// `Light` node (one whose `source` is NOT in the IR `handle_map` — i.e. not an
// emissive Mesh entity) into `s.lights` in `LoweredScene::lights` order, after
// the entities. In this fixture every light is a standalone `Light` node and no
// Mesh is emissive, so `s.lights[k]` is exactly the handle of the k-th standalone
// light in `ls.lights`. That gives a deterministic `NodeId -> Handle32` table we
// freeze independently of any renderer internals beyond the public `s.lights`.
//
// NOTE on includes (mirrors test_light_grouping): importing `aleph.lowering`
// transitively exposes `aleph.sheaf`, whose module fragment pulls in <algorithm>.
// Textually `#include <algorithm>` here triggers a GCC C++-modules mangling clash
// on std::vector<std::vector<...>> iterator <=>. So we deliberately do NOT include
// <algorithm> and hand-roll the tiny set/sort helpers we need.

using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::scene::Handle32;
using aleph::scene::Scene;
using aleph::types::Camera;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::LightKind;
using aleph::types::LocalTransform;
using aleph::types::Material;
using aleph::types::MaterialKind;
using aleph::types::Mesh;
using aleph::types::NodeId;
using aleph::types::QuadLocal;
using aleph::types::SphereLocal;
using aleph::types::Transform;

namespace {

// ── tiny <algorithm>-free helpers (see include note above) ───────────────────

[[nodiscard]] bool contains_id(const std::vector<NodeId>& v, NodeId x) {
    for (const NodeId e : v) {
        if (e == x) return true;
    }
    return false;
}

[[nodiscard]] bool contains_handle(const std::vector<Handle32>& v, Handle32 h) {
    for (const Handle32 e : v) {
        if (e.packed == h.packed) return true;
    }
    return false;
}

// ── graph builders ───────────────────────────────────────────────────────────

NodeId add_mesh(Graph& g, const char* name) {
    const NodeId id = g.alloc_node_id();
    Mesh m{id, std::string(name), 1};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));
    return id;
}

// A standalone Light node carrying an area-quad payload so the renderer surfaces
// it as a real emissive primitive (a quad) registered into `Scene::lights`.
NodeId add_light(Graph& g, const char* name, Vec3 anchor) {
    const NodeId id = g.alloc_node_id();
    Light l{id, LightKind::Area, std::string(name)};
    l.emission = Vec3{4, 4, 4};
    l.geometry = QuadLocal{anchor, Vec3{1, 0, 0}, Vec3{0, 0, 1}};
    g.insert_node(std::move(l));
    return id;
}

NodeId add_material(Graph& g) {
    const NodeId id = g.alloc_node_id();
    Material m{id, MaterialKind::Lambertian};
    m.albedo = Vec3{0.7f, 0.7f, 0.7f};
    m.emit   = Vec3{0, 0, 0};  // NOT emissive -> stays out of the light table
    g.insert_node(std::move(m));
    return id;
}

void edge(Graph& g, EdgeKind k, NodeId a, NodeId b) {
    auto r = g.add_edge(k, a, b);
    REQUIRE(r.has_value());
}

// A fully-lowerable multi-light scene with a NON-TRIVIAL grouping:
//   * cluster A: light la influences mesh ma                       -> group {la}
//   * cluster B: lights lb0, lb1 BOTH influence the same mesh mb    -> group {lb0,lb1}
//   * lone light lone with no Influences edge                       -> group {lone}
// Disjoint mesh regions (ma vs mb, no Adjacent) keep A and B apart; the shared
// mesh mb merges lb0+lb1; lone is a singleton. The Material is non-emissive and
// every light is a standalone `Light` node, so `s.lights` order == standalone
// `ls.lights` order, giving a clean NodeId -> Handle32 bridge.
struct MultiLight {
    Graph  g;
    NodeId root{}, cam{}, mat{};
    NodeId ma{}, mb{};
    NodeId la{}, lb0{}, lb1{}, lone{};
};

MultiLight make_multi_light() {
    MultiLight s;
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

    s.ma  = add_mesh(g, "ma");
    s.mb  = add_mesh(g, "mb");
    s.mat = add_material(g);

    s.la   = add_light(g, "la",   Vec3{-3, 2, 0});
    s.lb0  = add_light(g, "lb0",  Vec3{3, 2, 0});
    s.lb1  = add_light(g, "lb1",  Vec3{4, 2, 0});
    s.lone = add_light(g, "lone", Vec3{0, 5, 0});

    // Hierarchy: the root Transform Contains everything so the DFS reaches each
    // drawable and lower() succeeds.
    edge(g, EdgeKind::Contains, s.root, s.cam);
    edge(g, EdgeKind::Contains, s.root, s.ma);
    edge(g, EdgeKind::Contains, s.root, s.mb);
    edge(g, EdgeKind::Contains, s.root, s.la);
    edge(g, EdgeKind::Contains, s.root, s.lb0);
    edge(g, EdgeKind::Contains, s.root, s.lb1);
    edge(g, EdgeKind::Contains, s.root, s.lone);

    // Material resolution for both meshes.
    edge(g, EdgeKind::References, s.ma, s.mat);
    edge(g, EdgeKind::References, s.mb, s.mat);

    // Influences: la->ma (group {la}); lb0->mb, lb1->mb (group {lb0,lb1}); lone
    // is left UNCONNECTED (singleton {lone}).
    edge(g, EdgeKind::Influences, s.la,  s.ma);
    edge(g, EdgeKind::Influences, s.lb0, s.mb);
    edge(g, EdgeKind::Influences, s.lb1, s.mb);

    return s;
}

// Build the deterministic NodeId -> Handle32 table the way build_render_scene
// registers lights: each STANDALONE Light (source NOT in handle_map) is appended
// to s.lights in ls.lights order, after the entities. This fixture has no
// emissive Mesh, so every ls.lights entry is standalone and the i-th standalone
// light is s.lights[i]. We assert that one-to-one shape, then return the pairing.
struct LightBridge {
    std::vector<NodeId>   ids;       // standalone light source ids, in s.lights order
    std::vector<Handle32> handles;   // == s.lights
};

[[nodiscard]] LightBridge
bridge_lights(const aleph::lowering::LoweredScene& ls, const Scene& s) {
    LightBridge b;
    for (const auto& l : ls.lights) {
        // Skip emissive Meshes (handle-mapped) — they self-register as entities,
        // not as standalone light appends. None in this fixture, but keep the
        // bridge correct in general.
        if (ls.handle_map.get(l.source) != nullptr) continue;
        b.ids.push_back(l.source);
    }
    b.handles = s.lights;
    return b;
}

// Look up the Handle32 a given light NodeId maps to in the bridge.
[[nodiscard]] Handle32 handle_for(const LightBridge& b, NodeId id) {
    for (std::size_t i = 0; i < b.ids.size(); ++i) {
        if (b.ids[i] == id) return b.handles[i];
    }
    return Handle32{0xFFFFFFFFu};  // sentinel: not found
}

}  // namespace

// ── SPEC §5.3 — Scene.light_groups partitions s.lights consistently with
//    LoweredScene.light_groups (same grouping, correct NodeId -> Handle32). ────
TEST_CASE("lowering: Scene.light_groups mirrors LoweredScene.light_groups (handle mapping)") {
    MultiLight s = make_multi_light();

    auto lowered = aleph::lowering::lower(s.g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    // The IR carries the grouping lower() baked (== light_groups_of, per
    // test_light_grouping). Pin its shape so the scene comparison is meaningful:
    // three groups — {la}, {lb0,lb1}, {lone} — in insertion order.
    REQUIRE(ls.light_groups.size() == 3);
    REQUIRE(ls.light_groups[0].size() == 1);   // {la}
    REQUIRE(ls.light_groups[1].size() == 2);   // {lb0, lb1}
    REQUIRE(ls.light_groups[2].size() == 1);   // {lone}
    CHECK(ls.light_groups[0][0] == s.la);
    CHECK(contains_id(ls.light_groups[1], s.lb0));
    CHECK(contains_id(ls.light_groups[1], s.lb1));
    CHECK(ls.light_groups[2][0] == s.lone);

    // Sanity: lower() agrees with the standalone light_groups_of oracle.
    CHECK(aleph::lowering::light_groups_of(s.g).size() == ls.light_groups.size());

    // Build the RenderScene; this is the hop under test.
    const aleph::lowering::RenderScene rs = aleph::lowering::build_render_scene(ls);
    const Scene& scene = rs.scene;

    // The renderer surfaced exactly the four standalone lights as primitives.
    REQUIRE(scene.lights.size() == 4);
    REQUIRE(scene.quads.Qx.size() == 4);  // four area-quad emitters

    // The deterministic NodeId -> Handle32 bridge: one standalone light per
    // s.lights slot, in IR order.
    const LightBridge bridge = bridge_lights(ls, scene);
    REQUIRE(bridge.ids.size() == scene.lights.size());

    // ── SAME GROUPING ────────────────────────────────────────────────────────
    // Scene.light_groups has the same number of groups, in the same order, with
    // the same per-group size as the LoweredScene grouping.
    REQUIRE(scene.light_groups.size() == ls.light_groups.size());
    for (std::size_t i = 0; i < ls.light_groups.size(); ++i) {
        CHECK(scene.light_groups[i].size() == ls.light_groups[i].size());
    }

    // ── CORRECT NodeId -> Handle32 MAPPING ───────────────────────────────────
    // Group-by-group: each lowered NodeId maps to the bridged Handle32, and that
    // handle appears at the matching slot of the scene group (order preserved).
    for (std::size_t gi = 0; gi < ls.light_groups.size(); ++gi) {
        const auto& src_group   = ls.light_groups[gi];
        const auto& scene_group = scene.light_groups[gi];
        REQUIRE(scene_group.size() == src_group.size());
        for (std::size_t li = 0; li < src_group.size(); ++li) {
            const Handle32 expected = handle_for(bridge, src_group[li]);
            // The NodeId must be a real, registered light (bridge hit).
            REQUIRE(expected.packed != 0xFFFFFFFFu);
            // The mapped handle is a genuine emissive-quad handle in s.lights.
            CHECK(contains_handle(scene.lights, expected));
            // ...and lands at the corresponding slot of the scene group.
            CHECK(scene_group[li].packed == expected.packed);
        }
    }

    // ── PARTITION of s.lights ─────────────────────────────────────────────────
    // The scene groups partition EXACTLY the handles in s.lights: every light
    // handle appears in exactly one group, and no group holds a stray handle.
    {
        std::size_t total = 0;
        for (const auto& grp : scene.light_groups) total += grp.size();
        CHECK(total == scene.lights.size());  // no handle dropped or duplicated

        // Coverage: each s.lights handle is found in some group exactly once.
        for (const Handle32 h : scene.lights) {
            std::size_t hits = 0;
            for (const auto& grp : scene.light_groups) {
                if (contains_handle(grp, h)) ++hits;
            }
            CHECK(hits == 1);
        }
        // No invented handles: every grouped handle is a member of s.lights.
        for (const auto& grp : scene.light_groups) {
            for (const Handle32 h : grp) {
                CHECK(contains_handle(scene.lights, h));
            }
        }
    }
}

// A degenerate-but-valid scene (no Influences edges) must STILL carry a valid
// grouping through the build: each light its own singleton, mapped to its own
// handle, partitioning s.lights (SPEC §4.1 degenerate case + §5.3 carry-through).
TEST_CASE("lowering: Scene.light_groups carries singleton groups when no Influences") {
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0, LocalTransform{aleph::math::Mat4::identity()}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 0, 5};
    g.insert_node(std::move(c));

    const NodeId mesh = add_mesh(g, "m");
    const NodeId mat  = add_material(g);

    const NodeId l0 = add_light(g, "l0", Vec3{-2, 2, 0});
    const NodeId l1 = add_light(g, "l1", Vec3{0, 2, 0});
    const NodeId l2 = add_light(g, "l2", Vec3{2, 2, 0});

    edge(g, EdgeKind::Contains,   root, cam);
    edge(g, EdgeKind::Contains,   root, mesh);
    edge(g, EdgeKind::Contains,   root, l0);
    edge(g, EdgeKind::Contains,   root, l1);
    edge(g, EdgeKind::Contains,   root, l2);
    edge(g, EdgeKind::References, mesh, mat);
    // NOTE: deliberately NO Influences edges -> three singleton groups.

    auto lowered = aleph::lowering::lower(g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;

    // Three singleton groups in the IR.
    REQUIRE(ls.light_groups.size() == 3);
    for (const auto& grp : ls.light_groups) CHECK(grp.size() == 1);

    const aleph::lowering::RenderScene rs = aleph::lowering::build_render_scene(ls);
    const Scene& scene = rs.scene;

    REQUIRE(scene.lights.size() == 3);

    const LightBridge bridge = bridge_lights(ls, scene);
    REQUIRE(bridge.ids.size() == 3);

    // Same grouping shape carried into the Scene.
    REQUIRE(scene.light_groups.size() == 3);
    for (std::size_t i = 0; i < ls.light_groups.size(); ++i) {
        REQUIRE(scene.light_groups[i].size() == 1);
        const Handle32 expected = handle_for(bridge, ls.light_groups[i][0]);
        REQUIRE(expected.packed != 0xFFFFFFFFu);
        CHECK(scene.light_groups[i][0].packed == expected.packed);
        CHECK(contains_handle(scene.lights, expected));
    }

    // Partition: each of the three handles in its own group, exactly once.
    std::size_t total = 0;
    for (const auto& grp : scene.light_groups) total += grp.size();
    CHECK(total == scene.lights.size());
    for (const Handle32 h : scene.lights) {
        std::size_t hits = 0;
        for (const auto& grp : scene.light_groups) {
            if (contains_handle(grp, h)) ++hits;
        }
        CHECK(hits == 1);
    }
}
