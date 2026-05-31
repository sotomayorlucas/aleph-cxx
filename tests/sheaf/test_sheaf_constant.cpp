// Unit tests for aleph.sheaf:sheaf_constant.
//
// Oracles ported from aleph-engine/aleph-sheaf/src/constant_sheaf.rs
// (the #[cfg(test)] module).
//
// The Rust module has three unit tests:
//   * restriction_is_identity_1x1        -> ported verbatim below.
//   * h0_of_three_isolated_meshes_is_three
//   * h1_of_4_cycle_is_one
// The two H⁰/H¹ tests assert on `compute_hk(&sheaf, &cx, k).dim`. `compute_hk`
// lives in the :cohomology partition (Wave 3), which is not yet implemented in
// this port; those exact integer oracles (H⁰ = 3 for three isolated meshes,
// H¹ = 1 for the 4-cycle) are pinned in tests/sheaf/test_cohomology.cpp once
// :cohomology lands. Faking compute_hk here would improvise the cohomology
// math, so we do NOT do that. What this file CAN pin at the sheaf layer — and
// does — is that ConstantZ2Sheaf is exactly the constant Z/2 sheaf those
// oracles assume: dim_stalk ≡ 1 and every restriction is the 1×1 identity, so
// the resulting cochain complex is the cellular cochain complex over Z/2.
//
// Fixtures mirror the Rust `three_isolated_meshes` / `four_mesh_cycle` helpers,
// driven through the real Graph -> OneSkeleton -> FlagComplex pipeline so the
// simplices fed to the sheaf are genuine flag-complex simplices.

#include "doctest.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.linalg.gf2;

using aleph::graph::Graph;
using aleph::linalg::gf2::BitMatrix;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::ConstantZ2Sheaf;
using aleph::sheaf::FlagComplex;
using aleph::sheaf::make_simplex;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::Simplex;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

// Rust: three_isolated_meshes() — 3 Mesh nodes, no edges.
FlagComplex three_isolated_meshes() {
    Graph g;
    for (std::size_t i = 0; i < 3; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
    }
    return build_flag_complex(OneSkeleton::from_graph(g));
}

// Rust: four_mesh_cycle() — 4 Mesh nodes in an Adjacent 4-cycle.
FlagComplex four_mesh_cycle() {
    Graph g;
    std::vector<NodeId> ms;
    ms.reserve(4);
    for (std::size_t i = 0; i < 4; ++i) {
        const NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("m") + std::to_string(i), 1});
        ms.push_back(id);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const auto r = g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % 4]);
        REQUIRE(r.has_value());
    }
    return build_flag_complex(OneSkeleton::from_graph(g));
}

}  // namespace

// Oracle: constant_sheaf.rs::restriction_is_identity_1x1.
// Build the complex, take a 0-simplex, and check restriction(σ, σ) is the
// 1×1 identity. (Rust used `r.rows`/`r.cols`/`r.get(0,0)`; the C++
// BitMatrix exposes `rows()`/`cols()`/`at(r,c)`.)
TEST_CASE("restriction is identity 1x1") {
    const FlagComplex cx = three_isolated_meshes();
    REQUIRE_FALSE(cx.simplices.empty());
    REQUIRE_FALSE(cx.simplices[0].empty());

    const ConstantZ2Sheaf s(cx);
    const Simplex sigma = cx.simplices[0].front();  // first 0-simplex
    const BitMatrix r = s.restriction(sigma, sigma);

    CHECK(r.rows() == 1);
    CHECK(r.cols() == 1);
    CHECK(r.at(0, 0));
}

// The constant sheaf has stalk dimension 1 at EVERY simplex (Rust
// `stalk_dim` ≡ 1). Check across all dimensions present in the 4-cycle
// (vertices and edges). This is the property the H⁰/H¹ oracles rely on.
TEST_CASE("dim_stalk is one for every simplex") {
    const FlagComplex cx = four_mesh_cycle();
    const ConstantZ2Sheaf s(cx);

    bool saw_vertex = false;
    bool saw_edge = false;
    for (std::size_t k = 0; k < cx.simplices.size(); ++k) {
        for (const Simplex& sigma : cx.simplices[k]) {
            CHECK(s.dim_stalk(sigma) == 1);
            if (k == 0) saw_vertex = true;
            if (k == 1) saw_edge = true;
        }
    }
    // The 4-cycle has 4 vertices and 4 edges (and no 2-simplex), so both
    // levels must be non-empty for this check to be meaningful.
    CHECK(saw_vertex);
    CHECK(saw_edge);
    // 4-cycle: no triangle (matches flag_complex oracle: 4-cycle -> no 2-simplex).
    CHECK(cx.simplices.size() == 2);
}

// Restriction is the 1×1 identity for EVERY face relation, not just σ→σ.
// Check the edge-to-... face maps along the cycle: each edge restricts to
// its endpoints with the 1×1 identity.
TEST_CASE("every restriction is the 1x1 identity") {
    const FlagComplex cx = four_mesh_cycle();
    const ConstantZ2Sheaf s(cx);

    REQUIRE(cx.simplices.size() >= 2);
    const auto& edges = cx.simplices[1];
    const auto& verts = cx.simplices[0];
    REQUIRE_FALSE(edges.empty());
    REQUIRE_FALSE(verts.empty());

    for (const Simplex& e : edges) {
        for (const NodeId v : e) {
            const Simplex face = make_simplex({v});
            const BitMatrix r = s.restriction(face, e);
            CHECK(r.rows() == 1);
            CHECK(r.cols() == 1);
            CHECK(r.at(0, 0));
        }
    }
}

// lift_basis_index: both stalks are 1-dimensional and basis-aligned, so
// idx 0 ↦ 0 and any other index ↦ none. Mirrors the Rust
//   if idx == 0 { Some(0) } else { None }.
TEST_CASE("lift_basis_index aligns the single basis element") {
    const FlagComplex cx = three_isolated_meshes();
    const ConstantZ2Sheaf s(cx);
    const ConstantZ2Sheaf other(cx);

    REQUIRE_FALSE(cx.simplices.empty());
    REQUIRE_FALSE(cx.simplices[0].empty());
    const Simplex sigma = cx.simplices[0].front();

    const std::optional<std::size_t> lifted0 = s.lift_basis_index(sigma, 0, other);
    REQUIRE(lifted0.has_value());
    CHECK(lifted0.value() == 0);

    CHECK_FALSE(s.lift_basis_index(sigma, 1, other).has_value());
    CHECK_FALSE(s.lift_basis_index(sigma, 7, other).has_value());
}

// The borrowed complex is reachable through the accessor and is the one we
// constructed with (same shape: 3 isolated meshes -> 3 vertices, no edges).
TEST_CASE("sheaf borrows the complex it was built over") {
    const FlagComplex cx = three_isolated_meshes();
    const ConstantZ2Sheaf s(cx);

    CHECK(s.complex().simplices.size() == 1);          // only vertices
    CHECK(s.complex().simplices[0].size() == 3);       // three isolated meshes
    CHECK(s.complex().max_dim == 0);
}
