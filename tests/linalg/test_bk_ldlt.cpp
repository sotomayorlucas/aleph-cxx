#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

import aleph.linalg.sparse;

using aleph::linalg::sparse::BkError;
using aleph::linalg::sparse::BkErrorInfo;
using aleph::linalg::sparse::BkLdlt;
using aleph::linalg::sparse::DBlock;
using aleph::linalg::sparse::DMatrix;

namespace {

// diag(values) as an n x n DMatrix (port the Rust test helper).
DMatrix diag(const std::vector<double>& values) {
    const std::size_t n = values.size();
    DMatrix m = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        m.at(i, i) = values[i];
    }
    return m;
}

double norm(const std::vector<double>& v) {
    double s = 0.0;
    for (const double x : v) {
        s += x * x;
    }
    return std::sqrt(s);
}

double rel_err(const std::vector<double>& actual, const std::vector<double>& expected) {
    std::vector<double> diff(actual.size(), 0.0);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        diff[i] = actual[i] - expected[i];
    }
    return norm(diff) / std::max(norm(expected), 1e-12);
}

}  // namespace

// ---- factorize structure oracles (bk_ldlt.rs in-file tests) ----------------

TEST_CASE("bk_ldlt factorize diagonal matrix -> 3x One, d[i]==i+1") {
    const DMatrix h = diag({1.0, 2.0, 3.0});
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    CHECK(bk->n == 3);
    REQUIRE(bk->d.size() == 3);
    for (std::size_t i = 0; i < bk->d.size(); ++i) {
        REQUIRE_FALSE(bk->d[i].is_two);  // DBlock::One
        const double expected = static_cast<double>(i + 1);
        CHECK(std::abs(bk->d[i].d11 - expected) < 1e-12);
    }
}

TEST_CASE("bk_ldlt factorize dense SPD 3x3 -> d.size()==3 (all 1x1)") {
    DMatrix h = DMatrix::from_rows({{4.0, 2.0, 0.0}, {2.0, 5.0, 1.0}, {0.0, 1.0, 6.0}});
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    CHECK(bk->d.size() == 3);
}

TEST_CASE("bk_ldlt factorize rejects non-square (2x3) -> NotSquare") {
    const DMatrix m = DMatrix::zeros(2, 3);
    const auto bk = BkLdlt::factorize(m);
    REQUIRE_FALSE(bk.has_value());
    CHECK(bk.error().kind == BkError::NotSquare);
}

TEST_CASE("bk_ldlt factorize forced 2x2 pivot [[eps,1],[1,eps]] -> one DBlock::Two") {
    const double eps = 1e-3;
    DMatrix h = DMatrix::from_rows({{eps, 1.0}, {1.0, eps}});
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    REQUIRE(bk->d.size() == 1);
    CHECK(bk->d[0].is_two);  // DBlock::Two
}

TEST_CASE("bk_ldlt factorize indefinite 3x3 -> block dims sum to 3") {
    DMatrix h = DMatrix::from_rows({{1.0, 2.0, 3.0}, {2.0, -1.0, 4.0}, {3.0, 4.0, 5.0}});
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    std::size_t dim_sum = 0;
    for (const DBlock& b : bk->d) {
        dim_sum += b.size();
    }
    CHECK(dim_sum == 3);
}

// ---- round-trip oracles (bk_ldlt_properties.rs) ----------------------------

TEST_CASE("bk_ldlt round-trip diag(1,-2,3,-4) -> x=[1,-1,1,-1] rel<1e-9") {
    const DMatrix h = diag({1.0, -2.0, 3.0, -4.0});
    const std::vector<double> b = {1.0, 2.0, 3.0, 4.0};
    const std::vector<double> expected = {1.0, -1.0, 1.0, -1.0};
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    const auto x = bk->solve(b);
    REQUIRE(x.has_value());
    const double err = rel_err(*x, expected);
    CHECK(err < 1e-9);
}

TEST_CASE("bk_ldlt round-trip indefinite 3x3 -> ||Hx-b||/||b|| < 1e-9") {
    DMatrix h = DMatrix::from_rows({{1.0, 2.0, 3.0}, {2.0, -1.0, 4.0}, {3.0, 4.0, 5.0}});
    const std::vector<double> b = {6.0, 5.0, 12.0};
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    const auto x = bk->solve(b);
    REQUIRE(x.has_value());
    const std::vector<double> hx = h.matvec(*x);
    const double err = rel_err(hx, b);
    CHECK(err < 1e-9);
}

TEST_CASE("bk_ldlt round-trip dense indefinite 5x5 -> ||Hx-b||/||b|| < 1e-9") {
    DMatrix h = DMatrix::from_rows({{4.0, 1.0, -1.0, 2.0, 0.0},
                                    {1.0, -3.0, 1.0, 0.0, 1.0},
                                    {-1.0, 1.0, 2.0, -1.0, 0.0},
                                    {2.0, 0.0, -1.0, 5.0, 1.0},
                                    {0.0, 1.0, 0.0, 1.0, -2.0}});
    const std::vector<double> b = {1.0, 2.0, 3.0, 4.0, 5.0};
    const auto bk = BkLdlt::factorize(h);
    REQUIRE(bk.has_value());
    const auto x = bk->solve(b);
    REQUIRE(x.has_value());
    const std::vector<double> hx = h.matvec(*x);
    const double err = rel_err(hx, b);
    CHECK(err < 1e-9);
}

// ---- Dirichlet Green's function (relocated from analytical.rs) --------------
// DIRECT BkLdlt round-trip on a PD tridiagonal — NOT a Helmholtz/PSD test.

TEST_CASE("bk_ldlt Dirichlet Green's: tridiag(2,-1) 5x5, b[2]=1 -> [0.5,1,1.5,1,0.5]") {
    const std::size_t n_interior = 5;
    DMatrix l = DMatrix::zeros(n_interior, n_interior);
    for (std::size_t i = 0; i < n_interior; ++i) {
        l.at(i, i) = 2.0;
        if (i + 1 < n_interior) {
            l.at(i, i + 1) = -1.0;
            l.at(i + 1, i) = -1.0;
        }
    }
    const auto bk = BkLdlt::factorize(l);
    REQUIRE(bk.has_value());
    const std::size_t source_at = 2;
    std::vector<double> b(n_interior, 0.0);
    b[source_at] = 1.0;
    const auto phi = bk->solve(b);
    REQUIRE(phi.has_value());
    const double n = static_cast<double>(n_interior + 1);  // = 6
    for (std::size_t i = 0; i < n_interior; ++i) {
        const double lo = static_cast<double>(std::min(i + 1, source_at + 1));
        const double hi = static_cast<double>(std::max(i + 1, source_at + 1));
        const double expected = lo * (n - hi) / n;
        const double err = std::abs((*phi)[i] - expected);
        CHECK(err < 1e-9);
    }
    // Confirm the exact closed form [0.5, 1.0, 1.5, 1.0, 0.5].
    const std::vector<double> golden = {0.5, 1.0, 1.5, 1.0, 0.5};
    for (std::size_t i = 0; i < n_interior; ++i) {
        CHECK(std::abs((*phi)[i] - golden[i]) < 1e-9);
    }
}

// ---- determinism -----------------------------------------------------------

TEST_CASE("bk_ldlt determinism: factorize+solve twice -> byte-identical") {
    DMatrix h = DMatrix::from_rows({{4.0, 1.0, -1.0, 2.0, 0.0},
                                    {1.0, -3.0, 1.0, 0.0, 1.0},
                                    {-1.0, 1.0, 2.0, -1.0, 0.0},
                                    {2.0, 0.0, -1.0, 5.0, 1.0},
                                    {0.0, 1.0, 0.0, 1.0, -2.0}});
    const std::vector<double> b = {1.0, 2.0, 3.0, 4.0, 5.0};
    const auto bk1 = BkLdlt::factorize(h);
    const auto bk2 = BkLdlt::factorize(h);
    REQUIRE(bk1.has_value());
    REQUIRE(bk2.has_value());
    const auto x1 = bk1->solve(b);
    const auto x2 = bk2->solve(b);
    REQUIRE(x1.has_value());
    REQUIRE(x2.has_value());
    CHECK(*x1 == *x2);  // byte-identical
}
