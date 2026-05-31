#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

import aleph.containers;
import aleph.types;
import aleph.graph;
import aleph.dpo;

using namespace aleph::types;
using namespace aleph::dpo;
using aleph::graph::Graph;

// Build the canonical 8-node fixture (mirrors apps/aleph_graph_fixture):
// 1 root Transform, 1 child Transform, 1 Camera, 1 Light, 2 Meshes, 1
// Material referenced by both meshes, 1 Texture. 8 edges. Passes
// validate_all(g, 8).
namespace {

struct Fixture {
    Graph  g;
    NodeId root, child, cam, light, a, b, mat, tex;
};

Fixture make_fixture() {
    Fixture f;
    Graph& g = f.g;
    f.root  = g.alloc_node_id(); g.insert_node(Transform{f.root, 0});
    f.child = g.alloc_node_id(); g.insert_node(Transform{f.child, 1});
    f.cam   = g.alloc_node_id(); g.insert_node(Camera{f.cam, std::string("default")});
    f.light = g.alloc_node_id(); g.insert_node(Light{f.light, LightKind::Point, std::string("ies/std")});
    f.a     = g.alloc_node_id(); g.insert_node(Mesh{f.a, std::string("cube"), 12});
    f.b     = g.alloc_node_id(); g.insert_node(Mesh{f.b, std::string("sphere"), 320});
    f.mat   = g.alloc_node_id(); g.insert_node(Material{f.mat, MaterialKind::Lambertian});
    f.tex   = g.alloc_node_id(); g.insert_node(Texture{f.tex, 256, 256, TextureFormat::Rgb8});

    (void)g.add_edge(EdgeKind::Contains,   f.root,  f.child);
    (void)g.add_edge(EdgeKind::Contains,   f.child, f.a);
    (void)g.add_edge(EdgeKind::Contains,   f.child, f.b);
    (void)g.add_edge(EdgeKind::Contains,   f.root,  f.cam);
    (void)g.add_edge(EdgeKind::References, f.a,     f.mat);
    (void)g.add_edge(EdgeKind::References, f.b,     f.mat);
    (void)g.add_edge(EdgeKind::References, f.mat,   f.tex);
    (void)g.add_edge(EdgeKind::Influences, f.light, f.a);
    return f;
}

}  // namespace

TEST_CASE("fixture validates before any rewrite") {
    Fixture f = make_fixture();
    CHECK(f.g.node_count() == 8);
    CHECK(f.g.edge_count() == 8);
    CHECK(aleph::graph::validate_all(f.g, 8).has_value());
}

TEST_CASE("find_matches(spawn_light) finds >=1 mesh match with the right kind") {
    Fixture f = make_fixture();
    std::vector<Match> matches = find_matches(rules::spawn_light(), f.g);
    REQUIRE(matches.size() >= 1);

    // Every matched node must satisfy the LHS NodeKind (mesh).
    for (const Match& m : matches) {
        const NodeId* mesh = m.node_map.get(PatternNodeId{0});
        REQUIRE(mesh != nullptr);
        const Node* n = f.g.node(*mesh);
        REQUIRE(n != nullptr);
        CHECK(kind_of(*n) == NodeKind::Mesh);
    }
    // Two meshes in the fixture -> two distinct embeddings.
    CHECK(matches.size() == 2);
}

TEST_CASE("apply(spawn_light) adds a light + edge and keeps invariants") {
    Fixture f = make_fixture();
    const std::size_t n0 = f.g.node_count();
    const std::size_t e0 = f.g.edge_count();

    std::vector<Match> matches = find_matches(rules::spawn_light(), f.g);
    REQUIRE(matches.size() >= 1);

    auto res = apply(rules::spawn_light(), matches[0], f.g);
    REQUIRE(res.has_value());
    const RewriteRecord& rec = *res;

    CHECK(rec.created_nodes.size() == 1);
    CHECK(rec.created_edges.size() == 1);
    CHECK(rec.deleted_nodes.empty());
    CHECK(rec.deleted_edges.empty());

    CHECK(f.g.node_count() == n0 + 1);
    CHECK(f.g.edge_count() == e0 + 1);

    // The created node is a Light, present in the graph.
    const Node* created = f.g.node(rec.created_nodes[0]);
    REQUIRE(created != nullptr);
    CHECK(kind_of(*created) == NodeKind::Light);

    // Post-condition holds.
    CHECK(aleph::graph::validate_all(f.g, SIZE_MAX).has_value());
}

TEST_CASE("apply(refine_cell) splits a mesh into two children, invariants hold") {
    Fixture f = make_fixture();
    const std::size_t n0 = f.g.node_count();
    const std::size_t e0 = f.g.edge_count();

    std::vector<Match> matches = find_matches(rules::refine_cell(), f.g);
    REQUIRE(matches.size() >= 1);

    auto res = apply(rules::refine_cell(), matches[0], f.g);
    REQUIRE(res.has_value());
    const RewriteRecord& rec = *res;

    // 2 new meshes, 3 new edges (2 References + 1 Adjacent), no deletions.
    CHECK(rec.created_nodes.size() == 2);
    CHECK(rec.created_edges.size() == 3);
    CHECK(rec.deleted_nodes.empty());
    CHECK(rec.deleted_edges.empty());

    CHECK(f.g.node_count() == n0 + 2);
    CHECK(f.g.edge_count() == e0 + 3);
    CHECK(aleph::graph::validate_all(f.g, SIZE_MAX).has_value());
}

TEST_CASE("apply(remove_object) deletes a mesh and cascades incident edges") {
    Fixture f = make_fixture();
    const std::size_t n0 = f.g.node_count();

    std::vector<Match> matches = find_matches(rules::remove_object(), f.g);
    REQUIRE(matches.size() >= 1);

    // Pick the match on mesh b (the one with only a Contains + References
    // incident, no Influences) so the cascade is unambiguous; but any mesh
    // works. Use the first.
    auto res = apply(rules::remove_object(), matches[0], f.g);
    REQUIRE(res.has_value());
    const RewriteRecord& rec = *res;

    CHECK(rec.deleted_nodes.size() == 1);
    CHECK(rec.deleted_edges.size() >= 1);   // at least Contains + References
    CHECK(rec.created_nodes.empty());
    CHECK(rec.created_edges.empty());

    CHECK(f.g.node_count() == n0 - 1);
    CHECK(f.g.node(rec.deleted_nodes[0]) == nullptr);
    CHECK(aleph::graph::validate_all(f.g, 8).has_value());
}

TEST_CASE("transactional rollback: a rule that breaks CameraExclusive leaves g unchanged") {
    Fixture f = make_fixture();
    const std::size_t n0 = f.g.node_count();
    const std::size_t e0 = f.g.edge_count();

    // Custom rule: match a mesh, then CREATE a second Camera node. The
    // fixture already has exactly one Camera, so a second one violates the
    // CameraExclusive invariant -> apply must roll back.
    Rule bad;
    bad.name = "spawn_second_camera";
    bad.lhs.nodes.insert(PatternNodeId{0}, NodeConstraint{NodeKind::Mesh, std::nullopt});
    bad.interface = { PatternNodeId{0} };
    bad.rhs.nodes.insert(PatternNodeId{0}, NodeConstraint{NodeKind::Mesh, std::nullopt});
    bad.rhs.nodes.insert(PatternNodeId{1}, NodeConstraint{NodeKind::Camera, std::nullopt});
    AttrInit cam_init;
    cam_init.kind  = NodeKind::Camera;
    cam_init.build = [](NodeId id) -> Node {
        return Camera{id, std::string("rogue")};
    };
    bad.side_effects.push_back(CreateNode{PatternNodeId{1}, std::move(cam_init)});

    std::vector<Match> matches = find_matches(bad, f.g);
    REQUIRE(matches.size() >= 1);

    auto res = apply(bad, matches[0], f.g);
    REQUIRE_FALSE(res.has_value());
    CHECK(res.error() == ApplyError::InvariantViolation);

    // Snapshot restored: counts unchanged.
    CHECK(f.g.node_count() == n0);
    CHECK(f.g.edge_count() == e0);
    CHECK(aleph::graph::validate_all(f.g, 8).has_value());
}

TEST_CASE("find_matches is deterministic across repeated calls") {
    Fixture f = make_fixture();

    auto extract = [](const std::vector<Match>& ms) {
        std::vector<std::uint32_t> ids;
        for (const Match& m : ms) {
            const NodeId* mesh = m.node_map.get(PatternNodeId{0});
            ids.push_back(mesh ? mesh->value : 0xFFFFFFFFu);
        }
        return ids;
    };

    std::vector<Match> first  = find_matches(rules::spawn_light(), f.g);
    std::vector<Match> second = find_matches(rules::spawn_light(), f.g);

    REQUIRE(first.size() == second.size());
    CHECK(extract(first) == extract(second));
}

TEST_CASE("replace_material needs two materials; fixture has one -> no match") {
    Fixture f = make_fixture();
    // Fixture has a single Material, so the (old, new) material pair in the
    // LHS cannot be satisfied injectively.
    std::vector<Match> matches = find_matches(rules::replace_material(), f.g);
    CHECK(matches.empty());
}

TEST_CASE("replace_material reroutes the reference when a second material exists") {
    Fixture f = make_fixture();
    // Add a second material so replace_material can match (old, new) pair.
    NodeId mat2 = f.g.alloc_node_id();
    f.g.insert_node(Material{mat2, MaterialKind::Metal});
    REQUIRE(aleph::graph::validate_all(f.g, 8).has_value());

    const std::size_t n0 = f.g.node_count();
    const std::size_t e0 = f.g.edge_count();

    std::vector<Match> matches = find_matches(rules::replace_material(), f.g);
    REQUIRE(matches.size() >= 1);

    auto res = apply(rules::replace_material(), matches[0], f.g);
    REQUIRE(res.has_value());
    const RewriteRecord& rec = *res;
    // One edge deleted (old reference), one created (new reference).
    CHECK(rec.deleted_edges.size() == 1);
    CHECK(rec.created_edges.size() == 1);
    // Edge count net-unchanged, node count unchanged.
    CHECK(f.g.node_count() == n0);
    CHECK(f.g.edge_count() == e0);
    CHECK(aleph::graph::validate_all(f.g, 8).has_value());
}
