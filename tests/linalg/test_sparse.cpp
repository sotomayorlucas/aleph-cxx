#include "doctest.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

import aleph.linalg.sparse;

using aleph::linalg::sparse::CsrMatrix;
using aleph::linalg::sparse::DMatrix;
using aleph::linalg::sparse::LdltError;
using aleph::linalg::sparse::LDLT;
using aleph::linalg::sparse::SparseLdlt;
using aleph::linalg::sparse::elimination_tree;
using aleph::linalg::sparse::symbolic_factor;

namespace {

// xorshift64 PRNG mirroring the Rust oracle (sparse_dense_parity.rs).
double rng_next(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<double>(state >> 11) /
           static_cast<double>(std::uint64_t{1} << 53);
}

// M = A^T A + (n+1) I  =>  SPD. Port of random_spd from sparse_dense_parity.rs.
DMatrix random_spd(std::size_t n, std::uint64_t seed) {
    std::uint64_t state = seed | 1;
    DMatrix a = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            a.at(i, k) = rng_next(state) * 2.0 - 1.0;
        }
    }
    DMatrix m = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            double acc = 0.0;
            for (std::size_t r = 0; r < n; ++r) {
                acc += a.at(r, i) * a.at(r, k);
            }
            m.at(i, k) = acc;
        }
        m.at(i, i) = m.at(i, i) + static_cast<double>(n + 1);
    }
    return m;
}

double vec_rel_err(const std::vector<double>& actual, const std::vector<double>& expected) {
    double num = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const double d = actual[i] - expected[i];
        num += d * d;
    }
    num = std::sqrt(num);
    double den = 0.0;
    for (const double e : expected) {
        den += e * e;
    }
    den = std::sqrt(den);
    if (den < 1e-12) {
        den = 1e-12;
    }
    return num / den;
}

}  // namespace

// ─── Dense primitive sanity (kept from the original suite) ─────────────────

TEST_CASE("DMatrix matvec hand-computed 2x2") {
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0; a.at(0, 1) = 2.0;
    a.at(1, 0) = 3.0; a.at(1, 1) = 4.0;

    const std::array<double, 2> x{5.0, 6.0};
    const std::vector<double> y = a.matvec(std::span<const double>(x));

    REQUIRE(y.size() == 2);
    CHECK(y[0] == doctest::Approx(17.0));
    CHECK(y[1] == doctest::Approx(39.0));
}

TEST_CASE("DMatrix matmul + transpose hand-computed 2x2") {
    DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0; a.at(0, 1) = 2.0;
    a.at(1, 0) = 3.0; a.at(1, 1) = 4.0;

    DMatrix b(2, 2, 0.0);
    b.at(0, 0) = 5.0; b.at(0, 1) = 6.0;
    b.at(1, 0) = 7.0; b.at(1, 1) = 8.0;

    const DMatrix c = a.matmul(b);
    CHECK(c.at(0, 0) == doctest::Approx(19.0));
    CHECK(c.at(0, 1) == doctest::Approx(22.0));
    CHECK(c.at(1, 0) == doctest::Approx(43.0));
    CHECK(c.at(1, 1) == doctest::Approx(50.0));

    const DMatrix at = a.transpose();
    CHECK(at.at(0, 1) == doctest::Approx(3.0));
    CHECK(at.at(1, 0) == doctest::Approx(2.0));
}

// ─── DMatrix helpers ported from linalg_f64.rs ─────────────────────────────

TEST_CASE("DMatrix from_rows / identity / zeros / add / scale") {
    const DMatrix m = DMatrix::from_rows({{1.0, 2.0}, {3.0, 4.0}});
    REQUIRE(m.rows() == 2);
    REQUIRE(m.cols() == 2);
    CHECK(m.at(0, 1) == doctest::Approx(2.0));
    CHECK(m.at(1, 0) == doctest::Approx(3.0));

    const DMatrix id = DMatrix::identity(3);
    CHECK(id.at(0, 0) == doctest::Approx(1.0));
    CHECK(id.at(1, 1) == doctest::Approx(1.0));
    CHECK(id.at(0, 1) == doctest::Approx(0.0));

    const DMatrix z = DMatrix::zeros(2, 2);
    const DMatrix sum = m.add(z);
    CHECK(sum.approx_eq(m, 1e-12));

    const DMatrix doubled = m.scale(2.0);
    CHECK(doubled.at(0, 0) == doctest::Approx(2.0));
    CHECK(doubled.at(1, 1) == doctest::Approx(8.0));
    CHECK_FALSE(doubled.approx_eq(m, 1e-9));

    CHECK(m.frobenius_norm_diff(m) == doctest::Approx(0.0));
    CHECK(m.frobenius_norm_diff(z) == doctest::Approx(std::sqrt(1.0 + 4.0 + 9.0 + 16.0)));
}

// ─── LDLT (singular-PSD) ───────────────────────────────────────────────────

// Oracle: ldlt.rs::tests::ldlt_4x4_known — reconstruct ~ 1e-9.
TEST_CASE("ldlt_4x4_known reconstruct ~1e-9") {
    const DMatrix m = DMatrix::from_rows({
        {4.0, 2.0, 0.0, 0.0},
        {2.0, 5.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 9.0},
    });
    const auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());
    const DMatrix rec = f->reconstruct();
    CHECK(rec.approx_eq(m, 1e-9));
}

// Oracle: ldlt.rs::tests::ldlt_psd_kernel_recovers — singular PSD factorizes;
// last pivot is a kernel direction (|d| < PIVOT_EPS).
TEST_CASE("LDLT singular-PSD kernel recovers (does NOT error)") {
    const DMatrix m = DMatrix::from_rows({{1.0, -1.0}, {-1.0, 1.0}});
    const auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());
    const DMatrix rec = f->reconstruct();
    CHECK(rec.approx_eq(m, 1e-9));
    CHECK(std::abs(f->d[0]) > 1e-12);
    CHECK(std::abs(f->d[1]) < 1e-12);
}

// Oracle: ldlt.rs::tests::ldlt_solve_round_trip — 1e-9.
TEST_CASE("LDLT solve round trip ~1e-9") {
    const DMatrix m = DMatrix::from_rows({
        {4.0, 2.0, 0.0},
        {2.0, 5.0, 1.0},
        {0.0, 1.0, 3.0},
    });
    const auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());
    const std::array<double, 3> x_true{1.0, 2.0, 3.0};
    const std::vector<double> b = m.matvec(std::span<const double>(x_true));
    const auto x = f->solve(std::span<const double>(b));
    REQUIRE(x.has_value());
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK((*x)[i] == doctest::Approx(x_true[i]).epsilon(1e-9));
    }
}

// Singular Laplacian solve: b must be range-orthogonal to the constant kernel.
TEST_CASE("LDLT singular solve guards the kernel") {
    // Path-2 graph Laplacian: [[1,-1],[-1,1]], kernel = span{(1,1)}.
    const DMatrix m = DMatrix::from_rows({{1.0, -1.0}, {-1.0, 1.0}});
    const auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());

    // b = (0.5, -0.5) is in range (sums to zero) => solvable.
    const std::array<double, 2> b_ok{0.5, -0.5};
    const auto x = f->solve(std::span<const double>(b_ok));
    REQUIRE(x.has_value());
    const std::vector<double> r = m.matvec(std::span<const double>(*x));
    CHECK(r[0] == doctest::Approx(0.5).epsilon(1e-9));
    CHECK(r[1] == doctest::Approx(-0.5).epsilon(1e-9));

    // b = (1, 0) has a non-zero kernel component => out of range => nullopt.
    const std::array<double, 2> b_bad{1.0, 0.0};
    const auto none = f->solve(std::span<const double>(b_bad));
    CHECK_FALSE(none.has_value());
}

// Oracle: ldlt.rs::tests::ldlt_rank1_update_matches_fresh — Frobenius < 1e-7.
TEST_CASE("rank_1_update matches fresh factorize ~1e-7") {
    const DMatrix m = DMatrix::from_rows({
        {4.0, 2.0, 0.0},
        {2.0, 5.0, 1.0},
        {0.0, 1.0, 3.0},
    });
    const std::vector<double> v{1.0, 0.5, 0.25};
    const double alpha = 0.3;

    DMatrix m_perturbed = m;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            m_perturbed.at(i, j) = m.at(i, j) + alpha * v[i] * v[j];
        }
    }

    auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());
    const auto upd = f->rank_1_update(alpha, std::span<const double>(v));
    REQUIRE(upd.has_value());
    const DMatrix incremental = f->reconstruct();

    const auto fresh_f = LDLT::factorize(m_perturbed);
    REQUIRE(fresh_f.has_value());
    const DMatrix fresh = fresh_f->reconstruct();

    const double diff = incremental.frobenius_norm_diff(fresh);
    CHECK(diff < 1e-7);
}

// Oracle: ldlt.rs::tests::ldlt_rank3_update_matches_fresh — Frobenius < 1e-7.
TEST_CASE("rank_k_update (k=3) matches fresh factorize ~1e-7") {
    const DMatrix m = DMatrix::from_rows({
        {4.0, 1.0, 0.0, 0.0},
        {1.0, 4.0, 1.0, 0.0},
        {0.0, 1.0, 4.0, 1.0},
        {0.0, 0.0, 1.0, 4.0},
    });
    const std::vector<std::vector<double>> vs{
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    };
    const std::vector<double> alphas{0.1, 0.2, 0.3};

    DMatrix m_perturbed = m;
    for (std::size_t t = 0; t < alphas.size(); ++t) {
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                m_perturbed.at(i, j) = m_perturbed.at(i, j) + alphas[t] * vs[t][i] * vs[t][j];
            }
        }
    }

    auto f = LDLT::factorize(m);
    REQUIRE(f.has_value());
    const auto upd = f->rank_k_update(std::span<const double>(alphas),
                                      std::span<const std::vector<double>>(vs));
    REQUIRE(upd.has_value());
    const DMatrix incremental = f->reconstruct();

    const auto fresh_f = LDLT::factorize(m_perturbed);
    REQUIRE(fresh_f.has_value());
    const DMatrix fresh = fresh_f->reconstruct();

    const double diff = incremental.frobenius_norm_diff(fresh);
    CHECK(diff < 1e-7);
}

TEST_CASE("LDLT rejects non-symmetric and non-square") {
    // Non-square.
    {
        const DMatrix a(2, 3, 0.0);
        const auto f = LDLT::factorize(a);
        REQUIRE_FALSE(f.has_value());
        CHECK(f.error().kind == LdltError::NotSquare);
    }
    // Symmetric but indefinite ([[1,2],[2,1]], eigenvalues 3, -1): a negative
    // pivot is hit during elimination => NotPsd (singular-PSD is fine, but a
    // genuinely negative pivot is rejected).
    {
        const DMatrix a = DMatrix::from_rows({{1.0, 2.0}, {2.0, 1.0}});
        const auto f = LDLT::factorize(a);
        REQUIRE_FALSE(f.has_value());
        CHECK(f.error().kind == LdltError::NotPsd);
    }
    // Genuinely non-symmetric.
    {
        const DMatrix a = DMatrix::from_rows({{1.0, 2.0}, {0.0, 1.0}});
        const auto f = LDLT::factorize(a);
        REQUIRE_FALSE(f.has_value());
        CHECK(f.error().kind == LdltError::NotSymmetric);
    }
}

// ─── CSR ops (ported from sparse_csr_ops.rs) ───────────────────────────────

TEST_CASE("CSR from_dense->to_dense round trip ~1e-12") {
    DMatrix a(4, 4, 0.0);
    a.at(0, 0) = 3.0;              a.at(0, 3) = -1.0;
                     a.at(1, 1) = 2.5;
    a.at(2, 0) = 4.0; a.at(2, 2) = 1.0;
                                   a.at(3, 2) = -2.0; a.at(3, 3) = 5.0;

    const CsrMatrix csr = CsrMatrix::from_dense(a);
    const DMatrix back = csr.to_dense();
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            CHECK(std::abs(a.at(i, j) - back.at(i, j)) < 1e-12);
        }
    }
}

TEST_CASE("CSR get binary-search matches dense") {
    const DMatrix a = DMatrix::from_rows({
        {3.0, 0.0, 0.0, -1.0},
        {0.0, 2.5, 0.0, 0.0},
        {4.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, -2.0, 5.0},
    });
    const CsrMatrix csr = CsrMatrix::from_dense(a);
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            CHECK(csr.get(i, j) == doctest::Approx(a.at(i, j)));
        }
    }
}

TEST_CASE("CSR transpose round trip and from_dense_eps") {
    const DMatrix a = DMatrix::from_rows({
        {1.0, 2.0, 0.0},
        {0.0, 3.0, 4.0},
        {5.0, 0.0, 6.0},
    });
    const CsrMatrix csr = CsrMatrix::from_dense(a);
    const DMatrix tt = csr.transpose().transpose().to_dense();
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            CHECK(std::abs(a.at(i, j) - tt.at(i, j)) < 1e-12);
        }
    }
    // from_dense_eps drops near-zero entries.
    DMatrix noisy = a;
    noisy.at(0, 2) = 1e-16;
    const CsrMatrix csr_eps = CsrMatrix::from_dense_eps(noisy, 1e-15);
    CHECK(csr_eps.nnz() == csr.nnz());
}

TEST_CASE("CSR is_symmetric detects") {
    DMatrix sym(3, 3, 0.0);
    sym.at(0, 1) = 1.0; sym.at(1, 0) = 1.0;
    sym.at(0, 0) = 2.0; sym.at(1, 1) = 3.0; sym.at(2, 2) = 4.0;
    CHECK(CsrMatrix::from_dense(sym).is_symmetric(1e-12));

    DMatrix asym(2, 2, 0.0);
    asym.at(0, 1) = 1.0;
    asym.at(1, 0) = 2.0;
    CHECK_FALSE(CsrMatrix::from_dense(asym).is_symmetric(1e-12));
}

TEST_CASE("CSR empty has canonical row_ptr") {
    const CsrMatrix e = CsrMatrix::empty();
    CHECK(e.rows() == 0);
    CHECK(e.cols() == 0);
    CHECK(e.nnz() == 0);
    REQUIRE(e.row_ptr().size() == 1);
    CHECK(e.row_ptr()[0] == 0);
}

// ─── Sparse LDLT (ported from sparse_ldlt.rs + sparse_dense_parity.rs) ──────

// Oracle: sparse_ldlt.rs::tests::elimination_tree_of_diagonal_matrix_is_all_roots.
TEST_CASE("elimination_tree of diagonal = all roots") {
    DMatrix m = DMatrix::zeros(4, 4);
    m.at(0, 0) = 1.0; m.at(1, 1) = 1.0; m.at(2, 2) = 1.0; m.at(3, 3) = 1.0;
    const CsrMatrix csr = CsrMatrix::from_dense(m);
    const auto parent = elimination_tree(csr);
    REQUIRE(parent.size() == 4);
    for (const auto& p : parent) {
        CHECK_FALSE(p.has_value());
    }
}

// Oracle: sparse_ldlt.rs::tests::elimination_tree_of_tridiagonal_is_a_path.
TEST_CASE("elimination_tree of tridiagonal = path") {
    const std::size_t n = 5;
    DMatrix m = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        m.at(i, i) = 2.0;
        if (i + 1 < n) {
            m.at(i, i + 1) = -1.0;
            m.at(i + 1, i) = -1.0;
        }
    }
    const CsrMatrix csr = CsrMatrix::from_dense(m);
    const auto parent = elimination_tree(csr);
    const std::array<std::optional<std::size_t>, 5> expected{
        std::optional<std::size_t>{1}, std::optional<std::size_t>{2},
        std::optional<std::size_t>{3}, std::optional<std::size_t>{4},
        std::nullopt};
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(parent[i] == expected[i]);
    }
}

// Oracle: sparse_ldlt.rs::tests::symbolic_of_tridiagonal_has_subdiagonal_pattern.
TEST_CASE("symbolic_factor of tridiagonal has sub-diagonal pattern") {
    const std::size_t n = 5;
    DMatrix m = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        m.at(i, i) = 2.0;
        if (i + 1 < n) {
            m.at(i, i + 1) = -1.0;
            m.at(i + 1, i) = -1.0;
        }
    }
    const CsrMatrix csr = CsrMatrix::from_dense(m);
    const auto parent = elimination_tree(csr);
    const auto [col_ptr, row_idx] =
        symbolic_factor(csr, std::span<const std::optional<std::size_t>>(parent));
    const std::vector<std::size_t> exp_col_ptr{0, 1, 2, 3, 4, 4};
    const std::vector<std::size_t> exp_row_idx{1, 2, 3, 4};
    CHECK(col_ptr == exp_col_ptr);
    CHECK(row_idx == exp_row_idx);
}

// Oracle: sparse_ldlt.rs::tests::factor_and_solve_tridiagonal_5 — residual < 1e-9.
TEST_CASE("SparseLdlt factor + solve tridiagonal-5 residual < 1e-9") {
    const std::size_t n = 5;
    DMatrix m = DMatrix::zeros(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        m.at(i, i) = 2.0;
        if (i + 1 < n) {
            m.at(i, i + 1) = -1.0;
            m.at(i + 1, i) = -1.0;
        }
    }
    const CsrMatrix csr = CsrMatrix::from_dense(m);
    const auto f = SparseLdlt::factorize(csr);
    REQUIRE(f.has_value());
    const std::array<double, 5> b{1.0, 0.0, 0.0, 0.0, 0.0};
    const auto x = f->solve(std::span<const double>(b));
    REQUIRE(x.has_value());
    const std::vector<double> r = csr.matvec(std::span<const double>(*x));
    double res = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = r[i] - b[i];
        res += d * d;
    }
    res = std::sqrt(res);
    CHECK(res < 1e-9);
}

// Oracle: sparse_dense_parity.rs — sparse and dense solves agree on random SPD.
TEST_CASE("sparse solve matches dense solve on random SPD (5..15)") {
    for (std::size_t n = 5; n <= 15; ++n) {
        for (std::uint64_t seed = 1; seed <= 8; ++seed) {
            const DMatrix m = random_spd(n, seed);
            const auto dense = LDLT::factorize(m);
            REQUIRE(dense.has_value());
            const CsrMatrix csr = CsrMatrix::from_dense(m);
            const auto sparse = SparseLdlt::factorize(csr);
            REQUIRE(sparse.has_value());

            std::vector<double> b;
            b.reserve(n);
            std::uint64_t state = seed * 2654435761ULL;
            for (std::size_t i = 0; i < n; ++i) {
                b.push_back(rng_next(state));
            }
            const auto x_dense  = dense->solve(std::span<const double>(b));
            const auto x_sparse = sparse->solve(std::span<const double>(b));
            REQUIRE(x_dense.has_value());
            REQUIRE(x_sparse.has_value());
            const double err = vec_rel_err(*x_sparse, *x_dense);
            CHECK(err < 1e-9);
        }
    }
}

TEST_CASE("regression_ldlt_not_psd: negative pivot returns NotPsd") {
    DMatrix m = DMatrix::zeros(2, 2);
    m.at(0, 0) = 1.0;
    m.at(1, 1) = -1.0;
    const auto f = LDLT::factorize(m);
    CHECK_FALSE(f.has_value());
    REQUIRE(f.error().kind == LdltError::NotPsd);
}

TEST_CASE("regression_sparse_ldlt_near_singular: zero pivot accepted as kernel") {
    DMatrix m = DMatrix::zeros(3, 3);
    for (std::size_t i = 0; i < 3; ++i) m.at(i, i) = (i == 0) ? 1e-14 : 1.0;
    const CsrMatrix csr = CsrMatrix::from_dense(m);
    const auto f = SparseLdlt::factorize(csr);
    REQUIRE(f.has_value());
    const std::array<double, 3> b{1.0, 0.0, 0.0};
    const auto x = f->solve(std::span<const double>(b));
    REQUIRE(x.has_value());
}
