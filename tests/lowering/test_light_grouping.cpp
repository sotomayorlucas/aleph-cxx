#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import aleph.lowering;  // light_groups_of, lower, LoweredScene
import aleph.sheaf;     // VisibilitySheaf, compute_h0, OneSkeleton, H0, Component (ORACLE)
import aleph.graph;     // Graph
import aleph.types;     // NodeId / Mesh / Light / Material / Camera / EdgeKind ...
import aleph.math;      // Vec3

// Phase 5.x-a, Wave 1 — SPEC §5 tests 1, 2, 4.
//
// `light_groups_of(const Graph&)` partitions the graph's lights into GROUPS by
// the VisibilitySheaf's H⁰ connected components (SPEC §4.1):
//   * lights that co-influence a CONNECTED mesh region land in the SAME group;
//   * lights influencing DISJOINT regions are SEPARATE groups;
//   * a light with NO Influences edge is its OWN singleton group;
//   * order is deterministic (insertion order); two calls are byte-identical.
//
// The oracle (SPEC §5.1) is `aleph::sheaf::compute_h0` CROSS-CHECKED DIRECTLY:
// each H⁰ Component carries the lights touching a connected mesh region as
// `coherent_lights ∪ conflict_lights`, so the H⁰-derived light grouping is the
// per-component union of those light sets, plus a singleton for every light
// with no Influences edge (which appears in no component's stalk). We build the
// same skeleton/sheaf the lowering's `:grouping` partition does and assert the
// two partitions agree as SETS-OF-SETS.
//
// NOTE on includes: this TU imports `aleph.sheaf`, whose module fragment pulls
// in <algorithm>. Textually `#include <algorithm>` here as well triggers a GCC
// C++-modules mangling clash on std::vector<std::vector<NodeId>> iterator <=>.
// So we deliberately do NOT include <algorithm> and hand-roll the tiny
// sort/find/min helpers we need over NodeId (comparing `.value`).
//
// Architectural boundary (SPEC §1): the test cross-checks `light_groups_of`
// (which may import `aleph.sheaf`) against `compute_h0` directly. The renderer
// is NOT involved here — these are pure lowering/sheaf oracles.

using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::sheaf::Component;
using aleph::sheaf::H0;
using aleph::sheaf::LightSet;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::VisibilitySheaf;
using aleph::sheaf::compute_h0;
using aleph::types::Camera;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::LightKind;
using aleph::types::Material;
using aleph::types::MaterialKind;
using aleph::types::Mesh;
using aleph::types::NodeId;
using aleph::types::SphereLocal;

namespace {

// ── tiny <algorithm>-free helpers (see include note above) ───────────────────

[[nodiscard]] bool contains_id(const std::vector<NodeId>& v, NodeId x) {
    for (const NodeId e : v) {
        if (e == x) return true;
    }
    return false;
}

// Sort a vector<NodeId> ascending by value (insertion sort; group sizes here are
// tiny). Avoids std::sort to dodge the modules <=> mangling clash noted above.
void sort_ids(std::vector<NodeId>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        const NodeId key = v[i];
        std::size_t j = i;
        while (j > 0 && v[j - 1].value > key.value) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = key;
    }
}

[[nodiscard]] NodeId min_id(NodeId a, NodeId b) { return (a.value <= b.value) ? a : b; }
[[nodiscard]] NodeId max_id(NodeId a, NodeId b) { return (a.value >= b.value) ? a : b; }

// ── graph builders ──────────────────────────────────────────────────────────

NodeId add_mesh(Graph& g, const char* name) {
    const NodeId id = g.alloc_node_id();
    Mesh m{id, std::string(name), 1};
    m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
    g.insert_node(std::move(m));
    return id;
}

NodeId add_light(Graph& g, const char* name) {
    const NodeId id = g.alloc_node_id();
    Light l{id, LightKind::Point, std::string(name)};
    l.emission = Vec3{4, 4, 4};
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

// ── set-of-sets normalization for order-insensitive equality ─────────────────
//
// A group is a set of light NodeIds; the grouping is a set of groups. To compare
// `light_groups_of` against the H⁰-derived oracle as MATHEMATICAL partitions we
// canonicalize: each group sorted ascending, the list of groups sorted by their
// first (minimum) element. (Insertion ORDER is asserted separately in test 2.)
using Group = std::vector<NodeId>;
using Parts = std::vector<Group>;

// Lexicographic less over two ascending-sorted Groups, by NodeId value.
[[nodiscard]] bool group_less(const Group& a, const Group& b) {
    const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].value != b[i].value) return a[i].value < b[i].value;
    }
    return a.size() < b.size();
}

Parts canonical(Parts p) {
    for (Group& grp : p) sort_ids(grp);
    // insertion sort of the groups by group_less.
    for (std::size_t i = 1; i < p.size(); ++i) {
        Group key = p[i];
        std::size_t j = i;
        while (j > 0 && group_less(key, p[j - 1])) {
            p[j] = p[j - 1];
            --j;
        }
        p[j] = key;
    }
    return p;
}

// All Light nodes in the graph (node insertion order).
std::vector<NodeId> all_lights(const Graph& g) {
    std::vector<NodeId> out;
    for (auto [id, node] : g.nodes()) {
        if (aleph::types::kind_of(node) == aleph::types::NodeKind::Light) {
            out.push_back(id);
        }
    }
    return out;
}

// ── the ORACLE: light groups DERIVED from compute_h0 directly (SPEC §5.1) ─────
//
// Each H⁰ Component covers a connected mesh region; the lights touching that
// region are `coherent_lights ∪ conflict_lights`. That union is one light group
// (when non-empty). Every light NOT appearing in any component's union has no
// Influences edge into a connected region and is therefore its own singleton.
Parts oracle_groups_from_h0(const Graph& g) {
    const OneSkeleton skel  = OneSkeleton::from_graph(g);
    const VisibilitySheaf s = VisibilitySheaf::build_1_skeleton_only(g, skel);
    const H0 h0             = compute_h0(skel, s);

    Parts groups;
    std::vector<NodeId> grouped;  // lights already placed by a component

    for (const Component& c : h0.components) {
        LightSet lights = c.coherent_lights;  // sorted ascending
        for (const NodeId l : c.conflict_lights) {
            if (!contains_id(lights, l)) lights.push_back(l);
        }
        if (lights.empty()) continue;  // an unlit mesh region contributes no group
        groups.push_back(lights);
        for (const NodeId l : lights) grouped.push_back(l);
    }

    // Lights with no Influences edge appear in no component union -> singletons.
    for (const NodeId l : all_lights(g)) {
        if (!contains_id(grouped, l)) groups.push_back(Group{l});
    }
    return groups;
}

}  // namespace

// ── SPEC §5.1 — grouping_matches_h0 ──────────────────────────────────────────
//
// Two disjoint Light↔Mesh clusters + a shared mesh merging two lights + an
// unconnected light. `light_groups_of` must agree with the `compute_h0` oracle
// as a partition, and the disjoint-cluster scene must yield exactly 2 groups.
TEST_CASE("grouping: two disjoint Light<->Mesh clusters match compute_h0 (exactly 2 groups)") {
    Graph g;
    // Cluster A: light la influences mesh ma.
    const NodeId ma = add_mesh(g, "a");
    const NodeId la = add_light(g, "la");
    edge(g, EdgeKind::Influences, la, ma);
    // Cluster B: light lb influences mesh mb. No Adjacent between ma and mb ->
    // two disconnected mesh components -> two light groups.
    const NodeId mb = add_mesh(g, "b");
    const NodeId lb = add_light(g, "lb");
    edge(g, EdgeKind::Influences, lb, mb);

    const Parts groups = aleph::lowering::light_groups_of(g);

    // Exactly two groups, one per disjoint cluster.
    CHECK(groups.size() == 2);

    // Membership equals the compute_h0 component partition (SETS-OF-SETS).
    CHECK(canonical(groups) == canonical(oracle_groups_from_h0(g)));

    // Each group is a singleton holding its own cluster's light.
    const Parts c = canonical(groups);
    REQUIRE(c.size() == 2);
    CHECK(c[0].size() == 1);
    CHECK(c[1].size() == 1);
    const bool has_la = contains_id(c[0], la) || contains_id(c[1], la);
    const bool has_lb = contains_id(c[0], lb) || contains_id(c[1], lb);
    CHECK(has_la);
    CHECK(has_lb);
}

TEST_CASE("grouping: a shared mesh merges two lights into one group (matches compute_h0)") {
    Graph g;
    // Two lights both influence the SAME mesh -> one connected region -> one
    // group containing both lights.
    const NodeId m  = add_mesh(g, "shared");
    const NodeId l0 = add_light(g, "l0");
    const NodeId l1 = add_light(g, "l1");
    edge(g, EdgeKind::Influences, l0, m);
    edge(g, EdgeKind::Influences, l1, m);

    const Parts groups = aleph::lowering::light_groups_of(g);

    CHECK(groups.size() == 1);
    CHECK(canonical(groups) == canonical(oracle_groups_from_h0(g)));

    // The single group holds BOTH lights.
    const Parts c = canonical(groups);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].size() == 2);
    CHECK(c[0][0] == min_id(l0, l1));
    CHECK(c[0][1] == max_id(l0, l1));
}

TEST_CASE("grouping: lights over two Adjacent meshes form one group (connected region)") {
    Graph g;
    // ma --Adjacent-- mb is ONE connected region. l0 lights ma, l1 lights mb.
    // The component spans both meshes, so both lights co-influence the connected
    // region -> they merge into a single group (conflict_lights union).
    const NodeId ma = add_mesh(g, "a");
    const NodeId mb = add_mesh(g, "b");
    edge(g, EdgeKind::Adjacent, ma, mb);
    const NodeId l0 = add_light(g, "l0");
    const NodeId l1 = add_light(g, "l1");
    edge(g, EdgeKind::Influences, l0, ma);
    edge(g, EdgeKind::Influences, l1, mb);

    const Parts groups = aleph::lowering::light_groups_of(g);

    CHECK(groups.size() == 1);
    CHECK(canonical(groups) == canonical(oracle_groups_from_h0(g)));
    const Parts c = canonical(groups);
    REQUIRE(c.size() == 1);
    CHECK(c[0].size() == 2);
}

TEST_CASE("grouping: an unconnected light is its own singleton group (matches compute_h0)") {
    Graph g;
    // Cluster: la influences ma.
    const NodeId ma = add_mesh(g, "a");
    const NodeId la = add_light(g, "la");
    edge(g, EdgeKind::Influences, la, ma);
    // lone has NO Influences edge -> appears in no H⁰ component -> singleton.
    const NodeId lone = add_light(g, "lone");

    const Parts groups = aleph::lowering::light_groups_of(g);

    // Two groups: {la} and {lone}.
    CHECK(groups.size() == 2);
    CHECK(canonical(groups) == canonical(oracle_groups_from_h0(g)));

    // The unconnected light is alone in its group.
    bool lone_singleton = false;
    for (const Group& grp : groups) {
        if (grp.size() == 1 && grp[0] == lone) lone_singleton = true;
    }
    CHECK(lone_singleton);
}

// ── SPEC §5.2 — grouping_deterministic ───────────────────────────────────────
//
// Two calls are byte-identical, and group order follows insertion order.
TEST_CASE("grouping: two calls are byte-identical (deterministic)") {
    Graph g;
    const NodeId ma = add_mesh(g, "a");
    const NodeId mb = add_mesh(g, "b");
    const NodeId la = add_light(g, "la");
    const NodeId lb = add_light(g, "lb");
    edge(g, EdgeKind::Influences, la, ma);
    edge(g, EdgeKind::Influences, lb, mb);
    const NodeId lone = add_light(g, "lone");  // singleton
    (void)lone;

    const Parts a = aleph::lowering::light_groups_of(g);
    const Parts b = aleph::lowering::light_groups_of(g);

    // Identical structure AND identical ordering (NodeId.value compared exactly).
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].size() == b[i].size());
        for (std::size_t j = 0; j < a[i].size(); ++j) {
            CHECK(a[i][j].value == b[i][j].value);
        }
    }
}

TEST_CASE("grouping: group order follows insertion order") {
    // Build clusters whose FIRST-SEEN light goes la (id smaller) then lb, then a
    // singleton lone. The grouping order must track the order lights/components
    // are first encountered, NOT some hash order. The lights are allocated
    // la < lb < lone, each alone in its group, so the first element of each
    // group is monotonically increasing in insertion order.
    Graph g;
    const NodeId ma = add_mesh(g, "a");
    const NodeId la = add_light(g, "la");  // first cluster
    edge(g, EdgeKind::Influences, la, ma);
    const NodeId mb = add_mesh(g, "b");
    const NodeId lb = add_light(g, "lb");  // second cluster
    edge(g, EdgeKind::Influences, lb, mb);
    const NodeId lone = add_light(g, "lone");  // trailing singleton

    const Parts groups = aleph::lowering::light_groups_of(g);
    REQUIRE(groups.size() == 3);

    // Determinism: identical to a fresh call.
    const Parts again = aleph::lowering::light_groups_of(g);
    REQUIRE(again.size() == groups.size());

    // Insertion order: la's group precedes lb's group precedes lone's singleton.
    REQUIRE(groups[0].size() >= 1);
    REQUIRE(groups[1].size() >= 1);
    REQUIRE(groups[2].size() >= 1);
    CHECK(groups[0][0].value < groups[1][0].value);
    CHECK(groups[1][0].value < groups[2][0].value);
    CHECK(groups[0][0] == la);
    CHECK(groups[1][0] == lb);
    CHECK(groups[2][0] == lone);
}

// ── SPEC §5.4 — no_influences_degenerate ─────────────────────────────────────
//
// A graph with NO Influences edges: each light is its own singleton group, and
// the lowering still succeeds (degenerate-but-valid).
TEST_CASE("grouping: no Influences edges -> each light its own singleton group") {
    Graph g;
    // A valid lowerable scene with NO Influences edges: a camera, a mesh with a
    // resolved Material, and three contained lights.
    const NodeId root = g.alloc_node_id();
    g.insert_node(aleph::types::Transform{root, 0, aleph::types::LocalTransform{}});

    const NodeId cam = g.alloc_node_id();
    Camera c{cam, std::string("sensor0")};
    c.look_from = Vec3{0, 0, 5};
    g.insert_node(std::move(c));

    const NodeId mesh = add_mesh(g, "m");
    const NodeId mat  = add_material(g);

    const NodeId l0 = add_light(g, "l0");
    const NodeId l1 = add_light(g, "l1");
    const NodeId l2 = add_light(g, "l2");

    edge(g, EdgeKind::Contains,   root, cam);
    edge(g, EdgeKind::Contains,   root, mesh);
    edge(g, EdgeKind::Contains,   root, l0);
    edge(g, EdgeKind::Contains,   root, l1);
    edge(g, EdgeKind::Contains,   root, l2);
    edge(g, EdgeKind::References, mesh, mat);
    // NOTE: deliberately NO Influences edges.

    const Parts groups = aleph::lowering::light_groups_of(g);

    // Three lights -> three singleton groups.
    CHECK(groups.size() == 3);
    for (const Group& grp : groups) CHECK(grp.size() == 1);

    // Matches the compute_h0 oracle (no component carries any light -> all
    // lights become singletons).
    CHECK(canonical(groups) == canonical(oracle_groups_from_h0(g)));

    // Every light is present, exactly once, across the singletons.
    std::vector<NodeId> seen;
    for (const Group& grp : groups) seen.push_back(grp[0]);
    sort_ids(seen);
    REQUIRE(seen.size() == 3);
    CHECK(contains_id(seen, l0));
    CHECK(contains_id(seen, l1));
    CHECK(contains_id(seen, l2));

    // The lowering STILL SUCCEEDS on this degenerate-but-valid graph (SPEC §5.4:
    // "lowering still succeeds"). The handle/grouping CARRY-THROUGH into
    // LoweredScene.light_groups is test 3 (Wave 2), out of scope here.
    auto lowered = aleph::lowering::lower(g);
    REQUIRE(lowered.has_value());
    const aleph::lowering::LoweredScene& ls = *lowered;
    CHECK(ls.entities.size() == 1);  // the lone mesh lowered cleanly
    CHECK(ls.lights.size() == 3);    // three Light nodes in the light table
}
