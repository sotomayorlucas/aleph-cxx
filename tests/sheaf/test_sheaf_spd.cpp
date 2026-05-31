#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

import aleph.sheaf;
import aleph.graph;
import aleph.types;
import aleph.containers;

using aleph::graph::Graph;
using aleph::sheaf::Section;
using aleph::sheaf::SpdCohomology;
using aleph::sheaf::SpdSheaf;
using aleph::sheaf::build_constant;
using aleph::sheaf::build_per_cell;
using aleph::sheaf::compute_spd_cohomology;
using aleph::sheaf::connecting_morphism_spd;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::Node;
using aleph::types::NodeId;

using SectionMap = aleph::containers::OrderedMap<NodeId, Section>;

namespace {

NodeId add_mesh(Graph& g, const char* geo) {
    const NodeId id = g.alloc_node_id();
    g.insert_node(Node{Mesh{id, std::string(geo), 1}});
    return id;
}

// n-cycle of meshes; returns the allocated node ids in order. Mirrors the Rust
// `cycle_n` / `four_mesh_cycle` fixtures.
std::vector<NodeId> cycle_n(Graph& g, std::size_t n) {
    std::vector<NodeId> ms;
    ms.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        ms.push_back(add_mesh(g, ("m" + std::to_string(i)).c_str()));
    }
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, ms[i], ms[(i + 1) % n]).has_value());
    }
    return ms;
}

std::uint32_t bits_of(float f) noexcept {
    std::uint32_t b = 0;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

}  // namespace

// Oracle: spd_sheaf.rs::build_constant_produces_correct_section_count
TEST_CASE("build_constant produces correct section count") {
    Graph g;
    cycle_n(g, 4);
    const SpdSheaf sheaf = build_constant(g, Section{0.5f, 0.4f, 0.3f});
    CHECK(sheaf.get_sections().size() == 4);  // 4-cycle has 4 vertices
    for (const Section& v : sheaf.get_sections()) {
        CHECK(v[0] == doctest::Approx(0.5f));
        CHECK(v[1] == doctest::Approx(0.4f));
        CHECK(v[2] == doctest::Approx(0.3f));
    }
}

// Oracle: spd_sheaf.rs::build_per_cell_assigns_each_section
// Order-independent: check the multiset of triples matches the input.
TEST_CASE("build_per_cell assigns each section") {
    Graph g;
    const NodeId a = add_mesh(g, "m0");
    const NodeId b = add_mesh(g, "m1");
    const NodeId c = add_mesh(g, "m2");
    SectionMap sections;
    sections.insert(a, Section{0.9f, 0.1f, 0.1f});
    sections.insert(b, Section{0.1f, 0.9f, 0.1f});
    sections.insert(c, Section{0.1f, 0.1f, 0.9f});
    const SpdSheaf sheaf = build_per_cell(g, sections);
    REQUIRE(sheaf.get_sections().size() == 3);

    // Each input triple must appear exactly once in the output (multiset eq).
    auto count_eq = [&](const Section& target) {
        int n = 0;
        for (const Section& s : sheaf.get_sections()) {
            if (s[0] == doctest::Approx(target[0]) && s[1] == doctest::Approx(target[1]) &&
                s[2] == doctest::Approx(target[2])) {
                ++n;
            }
        }
        return n;
    };
    CHECK(count_eq(Section{0.9f, 0.1f, 0.1f}) == 1);
    CHECK(count_eq(Section{0.1f, 0.9f, 0.1f}) == 1);
    CHECK(count_eq(Section{0.1f, 0.1f, 0.9f}) == 1);
}

// Oracle: nodes absent from node_sections default to {0,0,0}.
TEST_CASE("build_per_cell defaults missing nodes to zero") {
    Graph g;
    const NodeId a = add_mesh(g, "m0");
    add_mesh(g, "m1");  // intentionally not in the section map
    SectionMap sections;
    sections.insert(a, Section{0.7f, 0.6f, 0.5f});
    const SpdSheaf sheaf = build_per_cell(g, sections);
    REQUIRE(sheaf.get_sections().size() == 2);
    int zeros = 0;
    int nonzeros = 0;
    for (const Section& s : sheaf.get_sections()) {
        if (s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f) {
            ++zeros;
        } else {
            ++nonzeros;
        }
    }
    CHECK(zeros == 1);
    CHECK(nonzeros == 1);
}

// Oracle: spd_sheaf.rs::cycle_h0_dim_is_1
TEST_CASE("cycle H0 dim is 1") {
    Graph g;
    cycle_n(g, 4);
    const SpdSheaf sheaf = build_constant(g, Section{0.5f, 0.5f, 0.5f});
    const SpdCohomology h = compute_spd_cohomology(sheaf);
    CHECK(h.h0_dim == 1);  // cycle is connected
}

// Oracle: spd_sheaf.rs::cycle_h1_dim_is_1
TEST_CASE("cycle H1 dim is 1") {
    Graph g;
    cycle_n(g, 4);
    const SpdSheaf sheaf = build_constant(g, Section{0.5f, 0.5f, 0.5f});
    const SpdCohomology h = compute_spd_cohomology(sheaf);
    CHECK(h.h1_dim == 1);  // 4-cycle has H^1 dim 1
}

// Oracle: spd_sheaf.rs::isolated_vertices_h0_three
TEST_CASE("isolated vertices H0 three") {
    Graph g;
    for (int i = 0; i < 3; ++i) add_mesh(g, ("m" + std::to_string(i)).c_str());
    const SpdSheaf sheaf = build_constant(g, Section{0.0f, 0.0f, 0.0f});
    const SpdCohomology h = compute_spd_cohomology(sheaf);
    CHECK(h.h0_dim == 3);  // 3 isolated vertices
    CHECK(h.h1_dim == 0);
}

// Oracle: spd_sheaf.rs::connecting_morphism_recovers_delta
TEST_CASE("connecting morphism recovers delta") {
    Graph g;
    cycle_n(g, 4);
    const SpdSheaf pre = build_constant(g, Section{0.5f, 0.3f, 0.1f});
    const SpdSheaf post = build_constant(g, Section{0.5f, 0.4f, 0.2f});
    const std::vector<Section> delta = connecting_morphism_spd(pre, post);
    REQUIRE(delta.size() == pre.get_sections().size());
    for (const Section& v : delta) {
        CHECK(std::abs(v[0] - 0.0f) < 1e-6f);
        CHECK(std::abs(v[1] - 0.1f) < 1e-6f);
        CHECK(std::abs(v[2] - 0.1f) < 1e-6f);
    }
}

// Oracle: tests/spd_sheaf.rs::per_cell_tinted_cycle_h1_nontrivial
TEST_CASE("per cell tinted cycle H1 nontrivial") {
    Graph g;
    const std::vector<NodeId> ms = cycle_n(g, 4);
    SectionMap sections;
    sections.insert(ms[0], Section{0.9f, 0.1f, 0.1f});  // red
    sections.insert(ms[1], Section{0.1f, 0.9f, 0.1f});  // green
    sections.insert(ms[2], Section{0.1f, 0.1f, 0.9f});  // blue
    sections.insert(ms[3], Section{0.9f, 0.5f, 0.1f});  // amber
    const SpdSheaf sheaf = build_per_cell(g, sections);
    const SpdCohomology h = compute_spd_cohomology(sheaf);
    REQUIRE(h.h1_dim == 1);
    const std::vector<Section>& generator = h.h1_generators[0];
    float max_abs = 0.0f;
    for (const Section& v : generator) {
        for (float coeff : v) {
            max_abs = std::max(max_abs, std::abs(coeff));
        }
    }
    CHECK(max_abs > 0.05f);  // tinted-cycle generator should carry colour
}

// Oracle: tests/spd_sheaf.rs::determinism_two_runs_identical
// Bit-identical H^1 generators across two independent runs.
TEST_CASE("determinism two runs identical") {
    Graph g;
    cycle_n(g, 6);
    const SpdSheaf s1 = build_constant(g, Section{0.7f, 0.4f, 0.2f});
    const SpdSheaf s2 = build_constant(g, Section{0.7f, 0.4f, 0.2f});
    const SpdCohomology h1 = compute_spd_cohomology(s1);
    const SpdCohomology h2 = compute_spd_cohomology(s2);
    CHECK(h1.h1_dim == h2.h1_dim);
    CHECK(h1.h0_dim == h2.h0_dim);
    REQUIRE(h1.h1_generators.size() == h2.h1_generators.size());
    for (std::size_t gi = 0; gi < h1.h1_generators.size(); ++gi) {
        const auto& a = h1.h1_generators[gi];
        const auto& b = h2.h1_generators[gi];
        REQUIRE(a.size() == b.size());
        for (std::size_t ei = 0; ei < a.size(); ++ei) {
            for (std::size_t k = 0; k < 3; ++k) {
                CHECK(bits_of(a[ei][k]) == bits_of(b[ei][k]));
            }
        }
    }
}

// Oracle: tests/spd_sheaf.rs::connecting_morphism_zero_under_identity_rewrite
TEST_CASE("connecting morphism zero under identity rewrite") {
    Graph g;
    cycle_n(g, 4);
    const SpdSheaf s = build_constant(g, Section{0.5f, 0.5f, 0.5f});
    const SpdSheaf s2 = build_constant(g, Section{0.5f, 0.5f, 0.5f});
    const std::vector<Section> delta = connecting_morphism_spd(s, s2);
    for (const Section& v : delta) {
        for (float coeff : v) {
            CHECK(std::abs(coeff) < 1e-6f);
        }
    }
}

// Plan oracle: the H^1 generator is the genuine closed-1-cochain produced by
// the Householder QR nullspace of δ₀^T. Two checks that the QR actually ran and
// produced a real nullspace vector (rather than a degenerate/zero column):
//   (1) the generator is non-trivial (the QR found a non-zero null vector);
//   (2) on a CONSTANT sheaf (value c on every vertex) the payload at each edge
//       is coeff_e * 0.5 * (c + c) = coeff_e * c, identical across all three
//       channels — so the three channels are exactly proportional. This is the
//       same channel-decoupling the Rust port relies on and indirectly pins the
//       QR reconstruction (Q's column carries one shared scalar coeff_e).
TEST_CASE("H1 generator is the QR null cochain on the 4-cycle") {
    Graph g;
    cycle_n(g, 4);
    const float c = 0.5f;
    const SpdSheaf sheaf = build_constant(g, Section{c, c, c});
    const SpdCohomology h = compute_spd_cohomology(sheaf);
    REQUIRE(h.h1_dim == 1);
    const std::vector<Section>& gen = h.h1_generators[0];
    REQUIRE(gen.size() == 4);  // 4 edges

    // (1) Non-trivial: QR produced a real nullspace vector.
    double norm2 = 0.0;
    for (const Section& e : gen) {
        const double coeff = static_cast<double>(e[0]) / static_cast<double>(c);
        norm2 += coeff * coeff;
    }
    CHECK(norm2 > 1e-6);

    // (2) On the constant sheaf each channel equals coeff_e * c, so the three
    // channels are exactly proportional edge-by-edge.
    for (const Section& e : gen) {
        CHECK(e[0] == doctest::Approx(e[1]).epsilon(1e-6));
        CHECK(e[1] == doctest::Approx(e[2]).epsilon(1e-6));
    }
}
