// Unit tests for aleph.sheaf:dec (M28 — Discrete Exterior Calculus).
//
// Oracles ported from the #[cfg(test)] module of
//   aleph-engine/aleph-sheaf/src/dec.rs
// There is no separate dec integration test in aleph-sheaf/tests, so dec.rs's
// inline tests are the ground truth:
//   * d_squared_is_zero_on_triangle_f32  -> d(d(0-form)) == 0 over R
//   * d_squared_is_zero_on_triangle_spd  -> d(d(0-form)) == 0 over R^3
//   * coeffs_zero_is_additive_identity   -> zero() is the additive identity
// The C++ provided impls are Z2(bool), R64(double), R3(Vec3) per spec 6.3, so
// the R oracle is realised at f64 (R64) and the d²=0 certificate is *also*
// pinned over Z2 (exact XOR arithmetic, no rounding).
//
// The flag complex is built through the real Graph -> OneSkeleton ->
// build_flag_complex pipeline (the stable public surface), mirroring the Rust
// `triangle_graph()` helper. The triangle gives simplices[0]=3 vertices,
// simplices[1]=3 edges, simplices[2]=1 triangle; a 0-form has 3 coefficients.
//
// Float exactness: every coefficient used is an exact integer or dyadic
// rational (1, 2, 4, 1/2, 1/4), so d(d(.)) lands *exactly* on 0 — the oracles
// are checked with `== zero()`, not weakened to a tolerance.

#include "doctest.h"
#include <cstddef>
#include <string>
#include <vector>

import aleph.sheaf;
import aleph.math;
import aleph.graph;
import aleph.types;

using aleph::graph::Graph;
using aleph::math::Vec3;
using aleph::sheaf::add_signed;
using aleph::sheaf::build_flag_complex;
using aleph::sheaf::Coeffs;
using aleph::sheaf::d;
using aleph::sheaf::FlagComplex;
using aleph::sheaf::Form;
using aleph::sheaf::OneSkeleton;
using aleph::sheaf::R3;
using aleph::sheaf::R64;
using aleph::sheaf::Z2;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

// The provided impls must satisfy the Coeffs concept (spec 6.3).
static_assert(Coeffs<Z2>);
static_assert(Coeffs<R64>);
static_assert(Coeffs<R3>);

namespace {

// Triangle of three mutually-Adjacent meshes — the Rust `triangle_graph()`.
// Edges 0-1, 1-2, 0-2 close into a 2-simplex.
FlagComplex triangle_complex() {
    Graph g;
    std::vector<NodeId> v;
    v.reserve(3);
    for (int i = 0; i < 3; ++i) {
        NodeId id = g.alloc_node_id();
        g.insert_node(Mesh{id, std::string("v") + std::to_string(i), 1});
        v.push_back(id);
    }
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[0], v[1]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[0], v[2]).has_value());
    REQUIRE(g.add_edge(EdgeKind::Adjacent, v[1], v[2]).has_value());
    return build_flag_complex(OneSkeleton::from_graph(g));
}

}  // namespace

// ── Coeffs algebra ───────────────────────────────────────────────────────────

// dec.rs::coeffs_zero_is_additive_identity (R component): zero().add(x) == x.
TEST_CASE("dec: R64 zero is additive identity") {
    const R64 z = R64::zero();
    CHECK(z.add(R64{3.5}) == R64{3.5});
    CHECK(R64{3.5}.add(z) == R64{3.5});
}

// dec.rs::coeffs_zero_is_additive_identity (R^3 component): vector zero adds
// componentwise to the identity.
TEST_CASE("dec: R3 zero is additive identity") {
    const R3 zv = R3::zero();
    const R3 out = zv.add(R3{Vec3{1.0f, 2.0f, 3.0f}});
    CHECK(out == R3{Vec3{1.0f, 2.0f, 3.0f}});
}

// Z/2: zero is identity, add is XOR, neg is the identity (-1 == 1 mod 2).
TEST_CASE("dec: Z2 ring axioms (XOR add, self-inverse neg)") {
    CHECK(Z2::zero() == Z2{false});
    CHECK(Z2::zero().add(Z2{true}) == Z2{true});
    CHECK(Z2{true}.add(Z2{true}) == Z2{false});   // 1 + 1 == 0
    CHECK(Z2{true}.neg() == Z2{true});            // -1 == 1
    CHECK(Z2{false}.neg() == Z2{false});
}

// add_signed default: positive -> add, negative -> add(neg). Over R the
// negative branch is subtraction.
TEST_CASE("dec: add_signed mirrors the Rust default") {
    CHECK(add_signed(R64{5.0}, R64{2.0}, true) == R64{7.0});
    CHECK(add_signed(R64{5.0}, R64{2.0}, false) == R64{3.0});
    // Over Z/2 sign is irrelevant (neg is identity): both branches XOR.
    CHECK(add_signed(Z2{true}, Z2{true}, true) == Z2{false});
    CHECK(add_signed(Z2{true}, Z2{true}, false) == Z2{false});
}

// ── d(d(form)) == 0 certificate ──────────────────────────────────────────────

// Sanity on the fixture shape: triangle has 3 vertices, 3 edges, 1 triangle.
TEST_CASE("dec: triangle complex has expected cell counts") {
    const FlagComplex cx = triangle_complex();
    REQUIRE(cx.simplices.size() == 3);
    CHECK(cx.simplices[0].size() == 3);
    CHECK(cx.simplices[1].size() == 3);
    CHECK(cx.simplices[2].size() == 1);
}

// dec.rs::d_squared_is_zero_on_triangle_f32 (realised at f64 / R64).
// Non-trivial 0-cochain (1, 2, 4) -> d -> d -> 0 on the single 2-cell.
TEST_CASE("dec: d(d(form)) == 0 over R64 (triangle, coeffs 1/2/4)") {
    const FlagComplex cx = triangle_complex();
    const Form<R64> c0{0, {R64{1.0}, R64{2.0}, R64{4.0}}};
    const Form<R64> c1 = d(cx, c0);
    const Form<R64> c2 = d(cx, c1);
    CHECK(c2.k == 2);
    REQUIRE(c2.coeffs.size() == 1);
    for (const R64& v : c2.coeffs) {
        CHECK(v == R64::zero());  // exact: integer coeffs, no rounding
    }
}

// dec.rs::d_squared_is_zero_on_triangle_spd. 0-cochain over R^3 with exact
// dyadic-rational components -> d² == 0 exactly (componentwise).
TEST_CASE("dec: d(d(form)) == 0 over R3 (triangle, dyadic coeffs)") {
    const FlagComplex cx = triangle_complex();
    const Form<R3> c0{
        0,
        {R3{Vec3{1.0f, 0.5f, 0.25f}},
         R3{Vec3{0.0f, 1.0f, 0.5f}},
         R3{Vec3{0.25f, 0.0f, 1.0f}}}};
    const Form<R3> c1 = d(cx, c0);
    const Form<R3> c2 = d(cx, c1);
    CHECK(c2.k == 2);
    REQUIRE(c2.coeffs.size() == 1);
    for (const R3& v : c2.coeffs) {
        CHECK(v == R3::zero());  // exact: dyadic components, no rounding
    }
}

// Same certificate over Z/2 — d² == 0 with exact XOR arithmetic. A 0-cochain
// that is 1 on each vertex must vanish under d² on the triangle's 2-cell.
TEST_CASE("dec: d(d(form)) == 0 over Z2 (triangle, all-ones 0-cochain)") {
    const FlagComplex cx = triangle_complex();
    const Form<Z2> c0{0, {Z2{true}, Z2{true}, Z2{true}}};
    const Form<Z2> c1 = d(cx, c0);
    const Form<Z2> c2 = d(cx, c1);
    CHECK(c2.k == 2);
    REQUIRE(c2.coeffs.size() == 1);
    for (const Z2& v : c2.coeffs) {
        CHECK(v == Z2::zero());
    }
}

// d² == 0 must hold for *every* 0-cochain, not just one witness. Enumerate all
// 2^3 = 8 Z/2 0-cochains on the triangle and check the 2-cell vanishes.
TEST_CASE("dec: d(d(form)) == 0 over Z2 for all 8 triangle 0-cochains") {
    const FlagComplex cx = triangle_complex();
    for (unsigned mask = 0; mask < 8u; ++mask) {
        const Form<Z2> c0{
            0,
            {Z2{(mask & 1u) != 0u},
             Z2{(mask & 2u) != 0u},
             Z2{(mask & 4u) != 0u}}};
        const Form<Z2> c2 = d(cx, d(cx, c0));
        REQUIRE(c2.coeffs.size() == 1);
        CHECK(c2.coeffs[0] == Z2::zero());
    }
}

// Degenerate codomain: d of a top-degree form (here the 2-form on the triangle)
// targets degree 3, which has no cells -> the zero cochain of size 0.
TEST_CASE("dec: d into an empty codomain yields the size-0 zero form") {
    const FlagComplex cx = triangle_complex();
    const Form<R64> c2{2, {R64{7.0}}};  // arbitrary 2-cochain
    const Form<R64> c3 = d(cx, c2);
    CHECK(c3.k == 3);
    CHECK(c3.coeffs.empty());
}
