// Unit tests for aleph.sheaf:sheaf_trait.
//
// This partition exports only the `CellularZ2Sheaf` concept (the C++26 port of
// Rust `trait CellularZ2Sheaf` in aleph-engine/aleph-sheaf/src/sheaf_trait.rs).
// There is no concrete sheaf in this partition, so the tests:
//
//   1. Exercise the concept against a minimal, in-test sheaf whose logic
//      mirrors the constant sheaf EXACTLY (constant_sheaf.rs): dim_stalk == 1,
//      restriction == identity(1), lift_basis_index(idx==0)==0 else nullopt.
//      The behavioural oracles are copied verbatim from the constant_sheaf.rs
//      `#[cfg(test)]` module (restriction_is_identity_1x1) and the trait impl
//      body (lift_basis_index: `if idx == 0 { Some(0) } else { None }`).
//   2. Confirm the concept REJECTS types that are missing a required method or
//      have the wrong return type — the concept's whole job is to constrain the
//      template surface used by the cochain/cohomology machinery.
//
// The math of cohomology dimensions (H⁰/H¹ tables in cohomology_regression.rs)
// is pinned in the cluster-C tests, which require the concrete sheaves; it is
// out of scope for the trait partition itself.

#include "doctest.h"
#include <cstddef>
#include <optional>
#include <string>

import aleph.sheaf;
import aleph.linalg.gf2;
import aleph.types;

using aleph::linalg::gf2::BitMatrix;
using aleph::sheaf::CellularZ2Sheaf;
using aleph::sheaf::make_simplex;
using aleph::sheaf::Simplex;
using aleph::types::NodeId;

namespace {

// Minimal model of CellularZ2Sheaf. Its three methods are a 1:1 transcription
// of ConstantZ2Sheaf in aleph-engine/aleph-sheaf/src/constant_sheaf.rs:
//
//   fn stalk_dim(&self, _sigma) -> usize { 1 }
//   fn restriction(&self, _sigma, _tau) -> BitMatrix { BitMatrix::identity(1) }
//   fn lift_basis_index(&self, _sigma, idx, _other) -> Option<usize> {
//       if idx == 0 { Some(0) } else { None }
//   }
struct MiniConstantSheaf {
    [[nodiscard]] std::size_t dim_stalk(const Simplex& /*sigma*/) const { return 1; }

    [[nodiscard]] BitMatrix restriction(const Simplex& /*sigma*/,
                                        const Simplex& /*tau*/) const {
        return BitMatrix::identity(1);
    }

    [[nodiscard]] std::optional<std::size_t> lift_basis_index(
        const Simplex& /*sigma*/, std::size_t idx,
        const MiniConstantSheaf& /*other*/) const {
        if (idx == 0) return std::optional<std::size_t>{0};
        return std::nullopt;
    }
};

// --- Negative models (must NOT satisfy the concept) --------------------------

// Missing restriction + lift_basis_index entirely.
struct NoRestriction {
    [[nodiscard]] std::size_t dim_stalk(const Simplex&) const { return 1; }
};

// dim_stalk returns the wrong type (int, not std::size_t) — std::same_as fails.
struct WrongDimStalkType {
    [[nodiscard]] int dim_stalk(const Simplex&) const { return 1; }
    [[nodiscard]] BitMatrix restriction(const Simplex&, const Simplex&) const {
        return BitMatrix::identity(1);
    }
    [[nodiscard]] std::optional<std::size_t> lift_basis_index(
        const Simplex&, std::size_t, const WrongDimStalkType&) const {
        return std::optional<std::size_t>{0};
    }
};

// lift_basis_index returns a bare std::size_t, not std::optional<std::size_t>.
struct WrongLiftReturn {
    [[nodiscard]] std::size_t dim_stalk(const Simplex&) const { return 1; }
    [[nodiscard]] BitMatrix restriction(const Simplex&, const Simplex&) const {
        return BitMatrix::identity(1);
    }
    [[nodiscard]] std::size_t lift_basis_index(const Simplex&, std::size_t,
                                               const WrongLiftReturn&) const {
        return 0;
    }
};

}  // namespace

TEST_CASE("CellularZ2Sheaf accepts a conforming sheaf") {
    static_assert(CellularZ2Sheaf<MiniConstantSheaf>,
                  "the constant-style sheaf must model CellularZ2Sheaf");
    CHECK(CellularZ2Sheaf<MiniConstantSheaf>);
}

TEST_CASE("CellularZ2Sheaf rejects non-conforming types") {
    static_assert(!CellularZ2Sheaf<NoRestriction>,
                  "missing restriction/lift_basis_index must fail the concept");
    static_assert(!CellularZ2Sheaf<WrongDimStalkType>,
                  "dim_stalk must return std::size_t exactly");
    static_assert(!CellularZ2Sheaf<WrongLiftReturn>,
                  "lift_basis_index must return std::optional<std::size_t>");
    CHECK(!CellularZ2Sheaf<NoRestriction>);
    CHECK(!CellularZ2Sheaf<WrongDimStalkType>);
    CHECK(!CellularZ2Sheaf<WrongLiftReturn>);
    // A plain non-class type cannot satisfy the member-expression requirements.
    CHECK(!CellularZ2Sheaf<int>);
}

// dim_stalk == 1 everywhere (constant_sheaf.rs: `fn stalk_dim(...) -> 1`).
TEST_CASE("constant-style dim_stalk is 1 on every simplex") {
    const MiniConstantSheaf s{};
    const Simplex vertex = make_simplex({NodeId{0}});
    const Simplex edge = make_simplex({NodeId{0}, NodeId{1}});
    const Simplex tri = make_simplex({NodeId{0}, NodeId{1}, NodeId{2}});
    CHECK(s.dim_stalk(vertex) == 1);
    CHECK(s.dim_stalk(edge) == 1);
    CHECK(s.dim_stalk(tri) == 1);
}

// Oracle: constant_sheaf.rs `restriction_is_identity_1x1`
//   r.rows == 1; r.cols == 1; r.get(0,0) == true.
TEST_CASE("constant-style restriction is the 1x1 identity") {
    const MiniConstantSheaf s{};
    const Simplex sigma = make_simplex({NodeId{0}});
    const BitMatrix r = s.restriction(sigma, sigma);
    CHECK(r.rows() == 1);
    CHECK(r.cols() == 1);
    CHECK(r.at(0, 0));
    // restriction rows × cols match dim_stalk(tau) × dim_stalk(sigma).
    CHECK(r.rows() == s.dim_stalk(sigma));
    CHECK(r.cols() == s.dim_stalk(sigma));
}

// Oracle: constant_sheaf.rs trait impl body
//   `if idx == 0 { Some(0) } else { None }` — both stalks 1-dim, basis-aligned.
TEST_CASE("constant-style lift_basis_index: 0 -> Some(0), else None") {
    const MiniConstantSheaf self{};
    const MiniConstantSheaf other{};
    const Simplex sigma = make_simplex({NodeId{0}});
    const auto lifted0 = self.lift_basis_index(sigma, 0, other);
    REQUIRE(lifted0.has_value());
    CHECK(lifted0.value() == 0);
    CHECK_FALSE(self.lift_basis_index(sigma, 1, other).has_value());
    CHECK_FALSE(self.lift_basis_index(sigma, 7, other).has_value());
}
