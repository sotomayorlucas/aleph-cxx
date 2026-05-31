#include "doctest.h"

#include <cmath>
#include <span>
#include <vector>

import aleph.flow;
import aleph.linalg.sparse;
import aleph.math;

using aleph::flow::wasserstein_1;
using aleph::linalg::sparse::DMatrix;
using aleph::math::f64;

namespace {

// Convenience: run wasserstein_1 over std::vector spans and require success.
f64 w1(const std::vector<f64>& mu, const std::vector<f64>& nu, const DMatrix& dist) {
    auto r = wasserstein_1(std::span<const f64>(mu), std::span<const f64>(nu), dist);
    REQUIRE(r.has_value());
    return *r;
}

}  // namespace

// Oracle values ported verbatim from aleph-engine/aleph-flow/src/wasserstein.rs
// (#[cfg(test)] module): tolerances and expected costs are copied exactly,
// NOT weakened.

TEST_CASE("wasserstein_dirac_zero") {
    const DMatrix dist = DMatrix::from_rows({{0.0}});
    const f64     w = w1({1.0}, {1.0}, dist);
    CHECK(std::abs(w) < 1e-9);
}

TEST_CASE("wasserstein_dirac_distance") {
    const DMatrix dist = DMatrix::from_rows({{0.0, 5.0}, {5.0, 0.0}});
    const f64     w = w1({1.0, 0.0}, {0.0, 1.0}, dist);
    CHECK(std::abs(w - 5.0) < 1e-9);
}

TEST_CASE("wasserstein_same_distribution") {
    const DMatrix dist = DMatrix::from_rows({{0.0, 1.0}, {1.0, 0.0}});
    const f64     w = w1({0.5, 0.5}, {0.5, 0.5}, dist);
    CHECK(std::abs(w) < 1e-9);
}

TEST_CASE("wasserstein_3x3_known") {
    const DMatrix dist =
        DMatrix::from_rows({{0.0, 1.0, 2.0}, {1.0, 0.0, 1.0}, {2.0, 1.0, 0.0}});
    const f64 w = w1({1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, dist);
    CHECK(std::abs(w - 2.0) < 1e-9);
}

TEST_CASE("wasserstein_4cycle_neighbors") {
    // mu_0 = (0, 0.5, 0, 0.5), mu_1 = (0.5, 0, 0.5, 0) on a 4-cycle.
    // Optimal transport: ship 0.5 from 1->0 (cost 0.5), ship 0.5 from 3->2
    // (cost 0.5). Total W_1 = 1.0.
    const DMatrix dist = DMatrix::from_rows({{0.0, 1.0, 2.0, 1.0},
                                             {1.0, 0.0, 1.0, 2.0},
                                             {2.0, 1.0, 0.0, 1.0},
                                             {1.0, 2.0, 1.0, 0.0}});
    const f64 w = w1({0.0, 0.5, 0.0, 0.5}, {0.5, 0.0, 0.5, 0.0}, dist);
    CHECK(std::abs(w - 1.0) < 1e-6);
}

// Symmetry: W_1(mu, nu) == W_1(nu, mu) for a symmetric cost matrix.
// (Oracle property named in the task brief; checked at the direct-solve
// tolerance 1e-9.)
TEST_CASE("wasserstein_symmetry") {
    const DMatrix dist =
        DMatrix::from_rows({{0.0, 1.0, 2.0}, {1.0, 0.0, 1.0}, {2.0, 1.0, 0.0}});
    const std::vector<f64> mu{1.0, 0.0, 0.0};
    const std::vector<f64> nu{0.0, 0.0, 1.0};
    const f64              w_forward = w1(mu, nu, dist);
    const f64              w_reverse = w1(nu, mu, dist);
    CHECK(std::abs(w_forward - w_reverse) < 1e-9);
}

// Zero-mismatch guard: incompatible shapes report an error rather than throw
// (aleph_flags_isa). The Rust reference asserts these preconditions.
TEST_CASE("wasserstein_shape_mismatch_reports_error") {
    const DMatrix          dist = DMatrix::from_rows({{0.0, 1.0}, {1.0, 0.0}});
    const std::vector<f64> mu{1.0, 0.0, 0.0};  // length 3 vs 2x2 dist
    const std::vector<f64> nu{0.0, 1.0};
    auto r = wasserstein_1(std::span<const f64>(mu), std::span<const f64>(nu), dist);
    CHECK_FALSE(r.has_value());
}
