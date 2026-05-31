#include "doctest.h"

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.dpo;

using aleph::containers::FlatSet;
using aleph::graph::Graph;
using aleph::sheaf::decompose_rewrite;
using aleph::sheaf::MayerVietorisCertificate;
using aleph::sheaf::mayer_vietoris_certify;
using aleph::sheaf::mayer_vietoris_certify_with;
using aleph::sheaf::SheafKind;
using aleph::sheaf::Subgraph;
using aleph::types::Camera;
using aleph::types::EdgeKind;
using aleph::types::Light;
using aleph::types::LightKind;
using aleph::types::Material;
using aleph::types::MaterialKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// ── Small graph builders (mirror the Rust test fixtures) ─────────────────────

NodeId add_mesh(Graph& g, const char* geo) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Mesh{id, std::string(geo), 1});
    return id;
}

NodeId add_light(Graph& g, const char* emit) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Light{id, LightKind::Point, std::string(emit)});
    return id;
}

NodeId add_material(Graph& g) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Material{id, MaterialKind::Lambertian});
    return id;
}

NodeId add_camera(Graph& g) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Camera{id, std::string("default")});
    return id;
}

void edge(Graph& g, EdgeKind k, NodeId a, NodeId b) {
    auto r = g.add_edge(k, a, b);
    REQUIRE(r.has_value());
}

// The canonical Mayer-Vietoris fixture (mayer_vietoris.rs::fixture): two meshes
// joined by an Adjacent edge, both referencing a material, one light
// influencing m1.
//
// Adaptation vs. the Rust reference: the C++ `aleph::dpo::apply` is
// transactional and commits only if `aleph::graph::validate_all` holds on the
// post-state (Graph::insert_node-style invariant gating), whereas the Rust
// `apply_rule` does not run the scene invariants. So the C++ fixture must be a
// *valid scene*, which the bare Rust fixture is not:
//   * `CameraExclusive` requires exactly one Camera → add `cam`.
//   * `replace_material` (C++ rule) reroutes a mesh reference from an existing
//     Material to a second existing Material, so the fixture needs a *second*
//     material `mat2` for the rule to match injectively (mirrors the dpo test).
// Both additions are invisible to the (Mesh, Adjacent) 1-skeleton the sheaf
// cohomology is computed over, so the Mayer-Vietoris residual is unchanged: the
// integer identity still closes at 0 on every rule.
struct MvFixture {
    Graph  g;
    NodeId m1, m2, mat, mat2, l, cam;
};

MvFixture make_mv_fixture() {
    MvFixture f;
    f.m1   = add_mesh(f.g, "a");
    f.m2   = add_mesh(f.g, "b");
    f.mat  = add_material(f.g);
    f.mat2 = add_material(f.g);  // second material so replace_material matches
    f.l    = add_light(f.g, "p");
    f.cam  = add_camera(f.g);    // CameraExclusive: exactly one Camera
    edge(f.g, EdgeKind::Adjacent, f.m1, f.m2);
    edge(f.g, EdgeKind::Influences, f.l, f.m1);
    edge(f.g, EdgeKind::References, f.m1, f.mat);
    edge(f.g, EdgeKind::References, f.m2, f.mat);
    return f;
}

// Run a rule against a fresh copy of the canonical fixture, decompose the
// rewrite into (U, K, R) with the TRUE preserved interface, and assert the
// certificate residual is zero (mayer_vietoris.rs::certify_rule). Returns the
// certificate so callers may make additional oracle assertions.
MayerVietorisCertificate certify_rule(const aleph::dpo::Rule& rule) {
    // g_before: a snapshot we keep read-only. g_after: a second build we apply
    // the rule onto (Graph is move-only, so we rebuild rather than clone).
    const MvFixture before = make_mv_fixture();
    MvFixture after = make_mv_fixture();

    std::vector<aleph::dpo::Match> matches = aleph::dpo::find_matches(rule, after.g);
    REQUIRE_MESSAGE(!matches.empty(), "rule has no match in the fixture");
    const aleph::dpo::Match& m = matches[0];

    // Snapshot the runtime image of the rule interface BEFORE applying: these
    // node ids survive the rewrite by construction.
    FlatSet<NodeId> preserved;
    for (const aleph::dpo::PatternNodeId p : rule.interface) {
        const NodeId* hid = m.node_map.get(p);
        REQUIRE(hid != nullptr);
        preserved.insert(*hid);
    }

    auto res = aleph::dpo::apply(rule, m, after.g);
    REQUIRE_MESSAGE(res.has_value(), "rule application failed");

    auto [u, k, r] = decompose_rewrite(before.g, after.g, preserved);
    const MayerVietorisCertificate cert = mayer_vietoris_certify(after.g, u, r, k);
    CHECK_MESSAGE(cert.residual == 0, "Mayer-Vietoris must close (residual 0)");
    return cert;
}

}  // namespace

// ── decompose_rewrite unit oracles (mayer_vietoris.rs::tests) ────────────────

// Empty graph + empty cover → all-zero certificate, residual 0.
TEST_CASE("empty graph certificate is all zero") {
    const Graph g;
    const Subgraph empty;
    const MayerVietorisCertificate cert = mayer_vietoris_certify(g, empty, empty, empty);
    CHECK(cert.h0_g_prime == 0);
    CHECK(cert.h0_u == 0);
    CHECK(cert.h0_r == 0);
    CHECK(cert.h0_k == 0);
    CHECK(cert.residual == 0);
}

// Identity rewrite (g_before == g_after, one preserved mesh): K, U, R each hold
// the single node; no edges in K or R.
TEST_CASE("decompose identity rewrite assigns all to U") {
    Graph before;
    const NodeId m = add_mesh(before, "x");
    Graph after;
    const NodeId m_after = add_mesh(after, "x");
    REQUIRE(m == m_after);  // same allocation order → same id

    FlatSet<NodeId> preserved;
    preserved.insert(m);

    auto [u, k, r] = decompose_rewrite(before, after, preserved);
    CHECK(k.node_ids.size() == 1);
    CHECK(k.edge_ids.empty());
    CHECK(u.node_ids.size() == 1);
    // R contains the preserved (interface) node, no created nodes.
    CHECK(r.node_ids.size() == 1);
    CHECK(r.edge_ids.empty());
}

// Pure addition: a new mesh appears only in g_after. The preserved mesh lands
// in U and R; the new mesh lands in R only (never U).
TEST_CASE("decompose pure addition puts new nodes in R only") {
    Graph before;
    const NodeId m = add_mesh(before, "x");

    Graph after;
    const NodeId m_after = add_mesh(after, "x");
    REQUIRE(m == m_after);
    const NodeId m2 = add_mesh(after, "y");

    FlatSet<NodeId> preserved;
    preserved.insert(m);

    auto [u, k, r] = decompose_rewrite(before, after, preserved);
    CHECK(k.node_ids.size() == 1);
    CHECK(u.node_ids.contains(m));
    CHECK_FALSE(u.node_ids.contains(m2));
    CHECK(r.node_ids.contains(m));
    CHECK(r.node_ids.contains(m2));
}

// A single lit mesh with U = {mesh}, empty K and R → residual 0.
TEST_CASE("single mesh no change certificate residual zero") {
    Graph g;
    const NodeId m = add_mesh(g, "x");
    Subgraph full;
    full.node_ids.insert(m);
    const Subgraph empty;
    const MayerVietorisCertificate cert = mayer_vietoris_certify(g, full, empty, empty);
    CHECK(cert.residual == 0);
}

// ── Per-rule MV certification on the canonical fixture (mayer_vietoris.rs) ────

TEST_CASE("MV holds for spawn_light") {
    (void)certify_rule(aleph::dpo::rules::spawn_light());
}

TEST_CASE("MV holds for remove_object") {
    (void)certify_rule(aleph::dpo::rules::remove_object());
}

TEST_CASE("MV holds for replace_material") {
    (void)certify_rule(aleph::dpo::rules::replace_material());
}

TEST_CASE("MV holds for refine_cell") {
    (void)certify_rule(aleph::dpo::rules::refine_cell());
}

// ── M2.5 counter-example closes under sheaf-aware H⁰ (connecting_morphism.rs) ─

// SPAWN_LIGHT on the seed {m1 —Adjacent— m2, both —References— mat} (no lights)
// once appeared to give residual = 1 under M2's compute_h0. Under sheaf-aware
// rank-nullity H⁰ + the connecting morphism ∂, the formula closes (residual 0).
TEST_CASE("M2.5 counter-example closes under sheaf cohomology") {
    // The C++ apply gates on validate_all, so the seed must be a valid scene:
    // a single Camera (CameraExclusive) is added. It is not a Mesh and carries
    // no Adjacent edge, so it never enters the (Mesh, Adjacent) 1-skeleton and
    // leaves the sheaf cohomology — hence the residual — unchanged.
    auto seed = [] {
        Graph g;
        const NodeId m1 = add_mesh(g, "a");
        const NodeId m2 = add_mesh(g, "b");
        const NodeId mat = add_material(g);
        (void)add_camera(g);
        edge(g, EdgeKind::Adjacent, m1, m2);
        edge(g, EdgeKind::References, m1, mat);
        edge(g, EdgeKind::References, m2, mat);
        return g;
    };
    const Graph before = seed();
    Graph after = seed();

    std::vector<aleph::dpo::Match> matches =
        aleph::dpo::find_matches(aleph::dpo::rules::spawn_light(), after);
    REQUIRE_FALSE(matches.empty());
    const aleph::dpo::Match& m = matches[0];

    FlatSet<NodeId> preserved;
    for (const aleph::dpo::PatternNodeId p : aleph::dpo::rules::spawn_light().interface) {
        const NodeId* hid = m.node_map.get(p);
        REQUIRE(hid != nullptr);
        preserved.insert(*hid);
    }

    auto res = aleph::dpo::apply(aleph::dpo::rules::spawn_light(), m, after);
    REQUIRE(res.has_value());

    auto [u, k, r] = decompose_rewrite(before, after, preserved);
    const MayerVietorisCertificate cert = mayer_vietoris_certify(after, u, r, k);
    CHECK_MESSAGE(cert.residual == 0, "MV must close under sheaf-aware H0");
}

// ── ε_sheaf > 0: triangulating a 4-cycle kills the constant-sheaf H¹ class ────

// THE M3B PAPER CLAIM (connecting_morphism.rs::
// triangulation_yields_nonzero_epsilon_sheaf_on_constant_sheaf): adding a
// diagonal to a 4-mesh cycle reduces the constant Z/2 sheaf's H¹ from 1 to 0.
// By Mayer-Vietoris exactness the connecting morphism ∂ must witness the dying
// class with rank ≥ 1, so ε_sheaf > 0 and residual == 0.
TEST_CASE("triangulation yields nonzero epsilon_sheaf on constant sheaf") {
    // g_before: 4-mesh cycle (m0-m1-m2-m3-m0), Adjacent edges only.
    Graph before;
    std::vector<NodeId> ms_b;
    for (int i = 0; i < 4; ++i) ms_b.push_back(add_mesh(before, "m"));
    for (std::size_t i = 0; i < 4; ++i)
        edge(before, EdgeKind::Adjacent, ms_b[i], ms_b[(i + 1) % 4]);

    // g_after: the same cycle plus one diagonal (m0-m2), splitting the square
    // into two triangles. Node ids match by allocation order.
    Graph after;
    std::vector<NodeId> ms_a;
    for (int i = 0; i < 4; ++i) ms_a.push_back(add_mesh(after, "m"));
    for (std::size_t i = 0; i < 4; ++i)
        edge(after, EdgeKind::Adjacent, ms_a[i], ms_a[(i + 1) % 4]);
    edge(after, EdgeKind::Adjacent, ms_a[0], ms_a[2]);  // the triangulating diagonal
    REQUIRE(ms_b == ms_a);

    // Preserved interface: all four cycle vertices survive the triangulation.
    FlatSet<NodeId> preserved;
    for (const NodeId n : ms_a) preserved.insert(n);

    auto [u, k, r] = decompose_rewrite(before, after, preserved);
    const MayerVietorisCertificate cert =
        mayer_vietoris_certify_with(after, u, r, k, SheafKind::ConstantZ2);

    CHECK_MESSAGE(cert.residual == 0, "MV must close under the constant sheaf");
    CHECK_MESSAGE(cert.epsilon_sheaf > 0,
                  "triangulating a 4-cycle must produce non-trivial epsilon_sheaf");
}
