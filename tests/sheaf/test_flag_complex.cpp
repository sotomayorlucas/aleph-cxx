// Unit tests for aleph.sheaf:flag_complex.
//
// Oracles ported verbatim from
//   aleph-engine/aleph-sheaf/src/flag_complex.rs   (the #[cfg(test)] module)
//   aleph-engine/aleph-sheaf/tests/flag_complex.rs (integration shape)
//
// The Rust unit tests exercise the private helpers `build_adjacency` and
// `bron_kerbosch` directly. Those are implementation detail in the C++ port
// (namespace aleph::sheaf::detail, unexported), so the equivalent oracles are
// pinned through the public `build_flag_complex` result, whose per-dimension
// counts are uniquely determined by the maximal-clique enumeration.
//
// Skeletons are built via OneSkeleton::from_graph over real Mesh+Adjacent
// graphs — the same stable public surface the Rust integration test uses —
// rather than poking OneSkeleton's fields directly.

#include "doctest.h"
#include <cstddef>
#include <string>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;

using aleph::graph::Graph;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::FlagComplex;
using aleph::sheaf::make_simplex;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::Simplex;
using aleph::sheaf::SimplexLess;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// Build a graph of `n` Mesh vertices with the given Adjacent edges (by index),
// returning its OneSkeleton. Mirrors the Rust `skel_of_edges` helper but via
// the real Graph -> OneSkeleton pipeline.
OneSkeleton mesh_skeleton(std::size_t n,
                          const std::vector<std::pair<std::size_t, std::size_t>>& edges) {
    Graph g;
    std::vector<NodeId> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
        v.push_back(id);
    }
    for (auto [a, b] : edges) {
        auto r = g.add_edge(EdgeKind::Adjacent, v[a], v[b]);
        REQUIRE(r.has_value());
    }
    return OneSkeleton::from_graph(g);
}

}  // namespace

// ── Bron-Kerbosch maximal-clique oracles (observed through build_flag_complex)

// flag_complex.rs::triangle_has_one_maximal_clique — three mutually adjacent
// meshes form exactly one maximal clique of size 3, i.e. exactly one 2-simplex.
TEST_CASE("flag_complex: triangle has one maximal clique (one 2-simplex)") {
    OneSkeleton s = mesh_skeleton(3, {{0, 1}, {1, 2}, {0, 2}});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.simplices[2].size() == 1);          // single 3-clique
    CHECK(cx.simplices[2][0].size() == 3);       // of three vertices
}

// flag_complex.rs::isolated_vertices_have_singleton_cliques — three meshes,
// no edges → three singleton maximal cliques → three 0-simplices, nothing else.
TEST_CASE("flag_complex: isolated vertices have singleton cliques") {
    OneSkeleton s = mesh_skeleton(3, {});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.max_dim == 0);
    CHECK(cx.simplices[0].size() == 3);
    for (const Simplex& c : cx.simplices[0]) {
        CHECK(c.size() == 1);
    }
}

// ── build_flag_complex oracles ──────────────────────────────────────────────

// flag_complex.rs::build_empty_skeleton_gives_empty_complex
TEST_CASE("flag_complex: empty skeleton gives empty complex") {
    OneSkeleton s = mesh_skeleton(0, {});
    FlagComplex cx = build_flag_complex(s);
    for (const auto& level : cx.simplices) {
        CHECK(level.empty());
    }
    CHECK(cx.max_dim == 0);  // convention: max_dim==0 even with no vertices
}

// flag_complex.rs::build_single_mesh_gives_one_vertex
TEST_CASE("flag_complex: single mesh gives one vertex") {
    OneSkeleton s = mesh_skeleton(1, {});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.max_dim == 0);
    CHECK(cx.simplices[0].size() == 1);
}

// flag_complex.rs::build_triangle_yields_max_dim_two_with_seven_simplices
TEST_CASE("flag_complex: triangle yields max_dim 2 with 3+3+1 simplices") {
    OneSkeleton s = mesh_skeleton(3, {{0, 1}, {1, 2}, {0, 2}});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.max_dim == 2);
    CHECK(cx.simplices[0].size() == 3);
    CHECK(cx.simplices[1].size() == 3);
    CHECK(cx.simplices[2].size() == 1);
    // 3 vertices + 3 edges + 1 triangle = 7 total simplices.
    std::size_t total = 0;
    for (const auto& level : cx.simplices) total += level.size();
    CHECK(total == 7);
}

// flag_complex.rs::build_k4_yields_max_dim_three_with_fifteen_simplices
TEST_CASE("flag_complex: K4 yields max_dim 3 with 4+6+4+1 simplices") {
    OneSkeleton s = mesh_skeleton(4, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.max_dim == 3);
    CHECK(cx.simplices[0].size() == 4);
    CHECK(cx.simplices[1].size() == 6);
    CHECK(cx.simplices[2].size() == 4);
    CHECK(cx.simplices[3].size() == 1);
    std::size_t total = 0;
    for (const auto& level : cx.simplices) total += level.size();
    CHECK(total == 15);  // 2^4 - 1
}

// flag_complex.rs::build_4_cycle_yields_max_dim_one — a 4-cycle has no 3-clique,
// so no 2-simplex; max_dim is 1.
TEST_CASE("flag_complex: 4-cycle yields max_dim 1 (no 2-simplex)") {
    OneSkeleton s = mesh_skeleton(4, {{0, 1}, {1, 2}, {2, 3}, {3, 0}});
    FlagComplex cx = build_flag_complex(s);
    CHECK(cx.max_dim == 1);
    CHECK(cx.simplices[0].size() == 4);
    CHECK(cx.simplices[1].size() == 4);
    // No 2-simplices: either level 2 is absent or empty.
    if (cx.simplices.size() > 2) {
        CHECK(cx.simplices[2].empty());
    }
}

// ── Determinism / canonicalisation guarantees (4c plan: levels sorted by
//    SimplexLess, deduped; output byte-stable) ────────────────────────────────

TEST_CASE("flag_complex: each level is sorted by SimplexLess and deduped") {
    OneSkeleton s = mesh_skeleton(4, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}});
    FlagComplex cx = build_flag_complex(s);
    const SimplexLess less{};
    for (const auto& level : cx.simplices) {
        for (std::size_t i = 1; i < level.size(); ++i) {
            // strictly increasing under SimplexLess => sorted AND no duplicates
            CHECK(less(level[i - 1], level[i]));
        }
        // every simplex is canonical: sorted-ascending vertices
        for (const Simplex& sx : level) {
            for (std::size_t j = 1; j < sx.size(); ++j) {
                CHECK(sx[j - 1] < sx[j]);
            }
        }
    }
}

TEST_CASE("flag_complex: build is deterministic across repeated runs") {
    OneSkeleton s = mesh_skeleton(4, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}});
    FlagComplex a = build_flag_complex(s);
    FlagComplex b = build_flag_complex(s);
    CHECK(a == b);
    // Edge-vertex symmetry of K4: the triangle level holds the four 2-faces.
    CHECK(a.simplices[2].size() == 4);
}

// Triangle's single 2-simplex is exactly {v0,v1,v2} (canonical order).
TEST_CASE("flag_complex: triangle top simplex is the canonical 3-set") {
    Graph g;
    std::vector<NodeId> v;
    for (int i = 0; i < 3; ++i) {
        NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
        v.push_back(id);
    }
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[0], v[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[1], v[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[0], v[2]).has_value());
    OneSkeleton s = OneSkeleton::from_graph(g);
    FlagComplex cx = build_flag_complex(s);
    REQUIRE(cx.simplices[2].size() == 1);
    CHECK(cx.simplices[2][0] == make_simplex(std::vector<NodeId>{v[0], v[1], v[2]}));
}
