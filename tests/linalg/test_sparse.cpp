#include "doctest.h"

#include <array>
#include <span>
#include <vector>

import aleph.linalg.sparse;

using aleph::linalg::sparse::CsrMatrix;
using aleph::linalg::sparse::DMatrix;
using aleph::linalg::sparse::LdltError;
using aleph::linalg::sparse::ldlt_factorize;
using aleph::linalg::sparse::ldlt_solve;

namespace {

// Reconstruct A = L * diag(D) * L^T from a factorization, for verification.
DMatrix reconstruct(const DMatrix& L, const std::vector<double>& D) {
    const std::size_t n = D.size();
    DMatrix A(n, n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                s += L.at(i, k) * D[k] * L.at(j, k);
            }
            A.at(i, j) = s;
        }
    }
    return A;
}

}  // namespace

TEST_CASE("DMatrix matvec hand-computed 2x2") {
    // A = [[1,2],[3,4]], x = [5,6]
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0; a.at(0, 1) = 2.0;
    a.at(1, 0) = 3.0; a.at(1, 1) = 4.0;

    const std::array<double, 2> x{5.0, 6.0};
    const std::vector<double> y = a.matvec(std::span<const double>(x));

    REQUIRE(y.size() == 2);
    CHECK(y[0] == doctest::Approx(17.0));  // 1*5 + 2*6
    CHECK(y[1] == doctest::Approx(39.0));  // 3*5 + 4*6
}

TEST_CASE("DMatrix matmul + transpose hand-computed 2x2") {
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0; a.at(0, 1) = 2.0;
    a.at(1, 0) = 3.0; a.at(1, 1) = 4.0;

    DMatrix b(2, 2, 0.0);
    b.at(0, 0) = 5.0; b.at(0, 1) = 6.0;
    b.at(1, 0) = 7.0; b.at(1, 1) = 8.0;

    const DMatrix c = a.matmul(b);
    REQUIRE(c.rows() == 2);
    REQUIRE(c.cols() == 2);
    CHECK(c.at(0, 0) == doctest::Approx(19.0));  // 1*5 + 2*7
    CHECK(c.at(0, 1) == doctest::Approx(22.0));  // 1*6 + 2*8
    CHECK(c.at(1, 0) == doctest::Approx(43.0));  // 3*5 + 4*7
    CHECK(c.at(1, 1) == doctest::Approx(50.0));  // 3*6 + 4*8

    const DMatrix at = a.transpose();
    CHECK(at.at(0, 0) == doctest::Approx(1.0));
    CHECK(at.at(0, 1) == doctest::Approx(3.0));
    CHECK(at.at(1, 0) == doctest::Approx(2.0));
    CHECK(at.at(1, 1) == doctest::Approx(4.0));
}

TEST_CASE("DMatrix matmul + matvec hand-computed 3x3") {
    // A = identity-ish row scaling: A = [[2,0,1],[0,3,0],[1,0,2]]
    DMatrix a(3, 3, 0.0);
    a.at(0, 0) = 2.0; a.at(0, 2) = 1.0;
    a.at(1, 1) = 3.0;
    a.at(2, 0) = 1.0; a.at(2, 2) = 2.0;

    const std::array<double, 3> x{1.0, 2.0, 3.0};
    const std::vector<double> y = a.matvec(std::span<const double>(x));
    CHECK(y[0] == doctest::Approx(2.0 * 1.0 + 1.0 * 3.0));  // 5
    CHECK(y[1] == doctest::Approx(3.0 * 2.0));              // 6
    CHECK(y[2] == doctest::Approx(1.0 * 1.0 + 2.0 * 3.0));  // 7
}

TEST_CASE("CSR from_dense then matvec equals dense matvec (4x4)") {
    // A "random-ish" sparse 4x4 with several exact zeros.
    DMatrix a(4, 4, 0.0);
    a.at(0, 0) = 3.0;             a.at(0, 3) = -1.0;
                     a.at(1, 1) = 2.5;
    a.at(2, 0) = 4.0; a.at(2, 2) = 1.0;
                                  a.at(3, 2) = -2.0; a.at(3, 3) = 5.0;

    const CsrMatrix csr = CsrMatrix::from_dense(a);
    CHECK(csr.rows() == 4);
    CHECK(csr.cols() == 4);
    // Exactly the non-zero count above.
    CHECK(csr.nnz() == 7);

    const std::array<double, 4> x{1.0, -2.0, 0.5, 3.0};
    const std::vector<double> y_dense = a.matvec(std::span<const double>(x));
    const std::vector<double> y_csr   = csr.matvec(std::span<const double>(x));

    REQUIRE(y_dense.size() == 4);
    REQUIRE(y_csr.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(y_csr[i] == doctest::Approx(y_dense[i]).epsilon(1e-12));
    }
}

TEST_CASE("LDL^T reconstruction of SPD 2x2") {
    // A = [[4,1],[1,3]]  (SPD)
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 4.0; a.at(0, 1) = 1.0;
    a.at(1, 0) = 1.0; a.at(1, 1) = 3.0;

    const auto fact = ldlt_factorize(a);
    REQUIRE(fact.has_value());

    // Known closed-form: D = [4, 2.75], L(1,0) = 0.25.
    CHECK(fact->D[0] == doctest::Approx(4.0).epsilon(1e-9));
    CHECK(fact->D[1] == doctest::Approx(2.75).epsilon(1e-9));
    CHECK(fact->L.at(0, 0) == doctest::Approx(1.0));
    CHECK(fact->L.at(1, 0) == doctest::Approx(0.25).epsilon(1e-9));
    CHECK(fact->L.at(1, 1) == doctest::Approx(1.0));

    const DMatrix rec = reconstruct(fact->L, fact->D);
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            CHECK(rec.at(i, j) == doctest::Approx(a.at(i, j)).epsilon(1e-9));
        }
    }
}

TEST_CASE("LDL^T reconstruction of SPD 3x3") {
    // A SPD 3x3 (diagonally dominant, symmetric).
    DMatrix a(3, 3, 0.0);
    a.at(0, 0) = 4.0; a.at(0, 1) = 1.0; a.at(0, 2) = 2.0;
    a.at(1, 0) = 1.0; a.at(1, 1) = 5.0; a.at(1, 2) = -1.0;
    a.at(2, 0) = 2.0; a.at(2, 1) = -1.0; a.at(2, 2) = 6.0;

    const auto fact = ldlt_factorize(a);
    REQUIRE(fact.has_value());

    const DMatrix rec = reconstruct(fact->L, fact->D);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            CHECK(rec.at(i, j) == doctest::Approx(a.at(i, j)).epsilon(1e-9));
        }
    }
}

TEST_CASE("ldlt_solve recovers x_true (A x = b)") {
    DMatrix a(3, 3, 0.0);
    a.at(0, 0) = 4.0; a.at(0, 1) = 1.0; a.at(0, 2) = 2.0;
    a.at(1, 0) = 1.0; a.at(1, 1) = 5.0; a.at(1, 2) = -1.0;
    a.at(2, 0) = 2.0; a.at(2, 1) = -1.0; a.at(2, 2) = 6.0;

    const std::array<double, 3> x_true{1.0, -2.0, 3.0};
    const std::vector<double> b = a.matvec(std::span<const double>(x_true));

    const auto fact = ldlt_factorize(a);
    REQUIRE(fact.has_value());

    const std::vector<double> x = ldlt_solve(*fact, std::span<const double>(b));
    REQUIRE(x.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(x[i] == doctest::Approx(x_true[i]).epsilon(1e-9));
    }
}

TEST_CASE("ldlt_factorize rejects non-SPD with NotPositiveDefinite") {
    // Symmetric but indefinite: [[1,2],[2,1]] has eigenvalues 3 and -1.
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0; a.at(0, 1) = 2.0;
    a.at(1, 0) = 2.0; a.at(1, 1) = 1.0;

    const auto fact = ldlt_factorize(a);
    REQUIRE_FALSE(fact.has_value());
    CHECK(fact.error() == LdltError::NotPositiveDefinite);
}

TEST_CASE("ldlt_factorize rejects non-square with NotSquare") {
    DMatrix a(2, 3, 0.0);
    const auto fact = ldlt_factorize(a);
    REQUIRE_FALSE(fact.has_value());
    CHECK(fact.error() == LdltError::NotSquare);
}
