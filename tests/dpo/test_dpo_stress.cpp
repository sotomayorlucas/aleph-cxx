#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <vector>

import aleph.math;
import aleph.containers;
import aleph.types;
import aleph.graph;
import aleph.dpo;

using namespace aleph::types;
using namespace aleph::dpo;
using aleph::graph::Graph;
using aleph::math::Vec3;

namespace {

struct Lcg {
    std::uint64_t state;
    explicit Lcg(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((next() >> 33) % n);
    }
};

Graph make_random_valid_graph(std::uint64_t seed) {
    Lcg rng(seed);
    Graph g;

    const NodeId root = g.alloc_node_id();
    g.insert_node(Transform{root, 0});

    const NodeId cam = g.alloc_node_id();
    g.insert_node(Camera{cam, std::string("c")});
    (void)g.add_edge(EdgeKind::Contains, root, cam);

    const std::uint32_t n_mesh = 1 + rng.below(4);
    for (std::uint32_t i = 0; i < n_mesh; ++i) {
        const NodeId mesh = g.alloc_node_id();
        Mesh m{mesh, std::string("m"), 0};
        m.geometry = SphereLocal{Vec3{0, 0, 0}, 1.0f};
        g.insert_node(std::move(m));

        const NodeId mat = g.alloc_node_id();
        Material mat_n{mat, MaterialKind::Lambertian};
        g.insert_node(std::move(mat_n));

        (void)g.add_edge(EdgeKind::Contains, root, mesh);
        (void)g.add_edge(EdgeKind::References, mesh, mat);
    }

    return g;
}

}  // namespace

TEST_CASE("dpo stress: apply refine_cell on 128 random graphs preserves invariants") {
    for (std::uint32_t i = 0; i < 128; ++i) {
        const std::uint64_t seed = 0xD000000000000001ULL ^ (0x9E3779B97F4A7C15ULL * (i + 1));
        Graph g = make_random_valid_graph(seed);

        std::vector<Match> matches = find_matches(rules::refine_cell(), g);
        if (matches.empty()) continue;

        const std::size_t n0 = g.node_count();
        const std::size_t e0 = g.edge_count();

        auto res = apply(rules::refine_cell(), matches[0], g);
        REQUIRE(res.has_value());

        CHECK(g.node_count() >= n0);
        CHECK(g.edge_count() >= e0);
        CHECK(aleph::graph::validate_all(g, g.node_count()).has_value());
    }
}

TEST_CASE("dpo stress: spawn_light on 64 random graphs adds valid light") {
    for (std::uint32_t i = 0; i < 64; ++i) {
        const std::uint64_t seed = 0xD000000000000002ULL ^ (0x9E3779B97F4A7C15ULL * (i + 1));
        Graph g = make_random_valid_graph(seed);

        std::vector<Match> matches = find_matches(rules::spawn_light(), g);
        if (matches.empty()) continue;

        const std::size_t n0 = g.node_count();
        auto res = apply(rules::spawn_light(), matches[0], g);
        REQUIRE(res.has_value());

        CHECK(g.node_count() == n0 + 1);
        CHECK(aleph::graph::validate_all(g, g.node_count()).has_value());

        const Node* created = g.node(res->created_nodes[0]);
        REQUIRE(created != nullptr);
        CHECK(kind_of(*created) == NodeKind::Light);
    }
}
