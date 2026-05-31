#include "doctest.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

import aleph.sheaf;
import aleph.types;

using aleph::sheaf::dim;
using aleph::sheaf::faces_of_dim;
using aleph::sheaf::make_simplex;
using aleph::sheaf::Simplex;
using aleph::sheaf::SimplexLess;
using aleph::types::NodeId;

namespace {
// C(n, r) — small exact binomial for the cardinality oracle.
std::size_t binom(std::size_t n, std::size_t r) {
    if (r > n) return 0;
    if (r > n - r) r = n - r;
    std::size_t num = 1, den = 1;
    for (std::size_t i = 0; i < r; ++i) {
        num *= (n - i);
        den *= (i + 1);
    }
    return num / den;
}
}  // namespace

// Oracle: simplex.rs `make_simplex_sorts_and_dedups`.
TEST_CASE("make_simplex sorts and dedups") {
    Simplex s = make_simplex({NodeId{2}, NodeId{0}, NodeId{2}, NodeId{1}});
    Simplex expected = {NodeId{0}, NodeId{1}, NodeId{2}};
    CHECK(s == expected);
}

// Oracle: simplex.rs `dim_zero_for_vertex`, plus edge/triangle from the plan.
TEST_CASE("dim is len-1 for vertex, edge, triangle") {
    CHECK(dim(Simplex{NodeId{0}}) == 0);
    CHECK(dim(make_simplex({NodeId{3}, NodeId{4}})) == 1);
    CHECK(dim(make_simplex({NodeId{0}, NodeId{1}, NodeId{2}})) == 2);
    // saturating_sub: empty simplex has dimension 0.
    CHECK(dim(Simplex{}) == 0);
}

// Oracle: simplex.rs `faces_of_triangle`.
TEST_CASE("faces of a triangle are its three edges") {
    Simplex t = make_simplex({NodeId{0}, NodeId{1}, NodeId{2}});
    std::vector<Simplex> edges = faces_of_dim(t, 1);
    REQUIRE(edges.size() == 3);

    auto contains = [&](const Simplex& e) {
        return std::find(edges.begin(), edges.end(), e) != edges.end();
    };
    CHECK(contains(Simplex{NodeId{0}, NodeId{1}}));
    CHECK(contains(Simplex{NodeId{0}, NodeId{2}}));
    CHECK(contains(Simplex{NodeId{1}, NodeId{2}}));

    // Determinism: lexicographic binomial order is exactly this sequence.
    std::vector<Simplex> expected = {
        {NodeId{0}, NodeId{1}},
        {NodeId{0}, NodeId{2}},
        {NodeId{1}, NodeId{2}},
    };
    CHECK(edges == expected);
}

// Oracle: simplex.rs `faces_of_dim_zero_returns_vertices` (order-exact).
TEST_CASE("faces of dim zero returns the vertices in order") {
    Simplex t = make_simplex({NodeId{5}, NodeId{7}});
    std::vector<Simplex> verts = faces_of_dim(t, 0);
    std::vector<Simplex> expected = {{NodeId{5}}, {NodeId{7}}};
    CHECK(verts == expected);
}

// Oracle (plan): faces_of_dim(s, k).size() == C(|s|, k+1); empty when k+1 > |s|.
TEST_CASE("faces_of_dim cardinality equals binomial coefficient") {
    Simplex s = make_simplex(
        {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}});
    REQUIRE(s.size() == 5);
    for (std::size_t k = 0; k < s.size(); ++k) {
        CHECK(faces_of_dim(s, k).size() == binom(5, k + 1));
    }
    // k+1 > |s| yields no faces.
    CHECK(faces_of_dim(s, 5).empty());
    CHECK(faces_of_dim(s, 99).empty());

    // The full-dimension face is the simplex itself.
    std::vector<Simplex> top = faces_of_dim(s, 4);
    REQUIRE(top.size() == 1);
    CHECK(top[0] == s);
}

// Oracle (plan): permutation-invariance of `==` and `std::hash<Simplex>`.
TEST_CASE("make_simplex makes vertex-set equality and hash permutation-invariant") {
    Simplex a = make_simplex({NodeId{2}, NodeId{0}, NodeId{1}});
    Simplex b = make_simplex({NodeId{1}, NodeId{2}, NodeId{0}});
    Simplex c = make_simplex({NodeId{0}, NodeId{0}, NodeId{1}, NodeId{2}, NodeId{2}});
    CHECK(a == b);
    CHECK(a == c);

    std::hash<Simplex> h{};
    CHECK(h(a) == h(b));
    CHECK(h(a) == h(c));

    // Different vertex sets should not be equal.
    Simplex d = make_simplex({NodeId{0}, NodeId{1}, NodeId{3}});
    CHECK_FALSE(a == d);
}

// Oracle (plan): SimplexLess is a lexicographic strict-weak ordering.
TEST_CASE("SimplexLess orders simplices lexicographically") {
    SimplexLess less{};
    Simplex s01 = make_simplex({NodeId{0}, NodeId{1}});
    Simplex s02 = make_simplex({NodeId{0}, NodeId{2}});
    Simplex s12 = make_simplex({NodeId{1}, NodeId{2}});
    Simplex s0 = make_simplex({NodeId{0}});

    CHECK(less(s01, s02));
    CHECK(less(s02, s12));
    CHECK(less(s0, s01));         // prefix is smaller
    CHECK_FALSE(less(s01, s01));  // irreflexive
    CHECK_FALSE(less(s12, s01));
}
