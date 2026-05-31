module;
#include <cstddef>
#include <expected>
#include <span>
#include <utility>
#include <vector>

export module aleph.linalg.sparse:ldlt;

import aleph.math;
import :dense;

export namespace aleph::linalg::sparse {

using aleph::math::f64;

enum class LdltError {
    NotSquare,
    NotPositiveDefinite,
};

// Dense LDL^T factorization of a symmetric positive-definite matrix A:
//   A = L * diag(D) * L^T, with L unit-lower-triangular (L(i,i) == 1).
// Only the lower triangle of A is read; the upper triangle is assumed
// symmetric.
struct LdltFactorization {
    DMatrix          L;  // unit-lower-triangular, n x n
    std::vector<f64> D;  // diagonal, length n
};

// Forward declarations so the definitions below aren't flagged by
// -Wmissing-declarations: non-inline free functions need a prior declaration
// (constexpr/inline ones elsewhere are exempt). Keeps the zero-warning gate.
[[nodiscard]] std::expected<LdltFactorization, LdltError>
ldlt_factorize(const DMatrix& a);
[[nodiscard]] std::vector<f64>
ldlt_solve(const LdltFactorization& f, std::span<const f64> b);

// Factorize an SPD matrix. Returns NotSquare for non-square input and
// NotPositiveDefinite when a non-positive pivot is encountered.
[[nodiscard]] std::expected<LdltFactorization, LdltError>
ldlt_factorize(const DMatrix& a) {
    const std::size_t n = a.rows();
    if (a.cols() != n) {
        return std::unexpected(LdltError::NotSquare);
    }

    DMatrix L(n, n, 0.0);
    std::vector<f64> D(n, 0.0);

    for (std::size_t j = 0; j < n; ++j) {
        // d_j = a_jj - sum_{k<j} L(j,k)^2 * D(k)
        f64 dj = a.at(j, j);
        for (std::size_t k = 0; k < j; ++k) {
            dj -= L.at(j, k) * L.at(j, k) * D[k];
        }
        if (!(dj > 0.0)) {
            // Non-positive pivot (also catches NaN): not positive definite.
            return std::unexpected(LdltError::NotPositiveDefinite);
        }
        D[j]      = dj;
        L.at(j, j) = 1.0;

        // L(i,j) = (a_ij - sum_{k<j} L(i,k)*L(j,k)*D(k)) / d_j, for i > j.
        for (std::size_t i = j + 1; i < n; ++i) {
            f64 s = a.at(i, j);
            for (std::size_t k = 0; k < j; ++k) {
                s -= L.at(i, k) * L.at(j, k) * D[k];
            }
            L.at(i, j) = s / dj;
        }
    }

    return LdltFactorization{std::move(L), std::move(D)};
}

// Solve A x = b given an LDL^T factorization, via:
//   forward solve  L y = b
//   diagonal solve D z = y
//   back solve     L^T x = z
[[nodiscard]] std::vector<f64>
ldlt_solve(const LdltFactorization& f, std::span<const f64> b) {
    const std::size_t n = f.D.size();
    std::vector<f64> x(n, 0.0);

    // Forward substitution: L y = b (L unit-lower-triangular). Store y in x.
    for (std::size_t i = 0; i < n; ++i) {
        f64 s = b[i];
        for (std::size_t k = 0; k < i; ++k) {
            s -= f.L.at(i, k) * x[k];
        }
        x[i] = s;  // L(i,i) == 1
    }

    // Diagonal solve: D z = y.
    for (std::size_t i = 0; i < n; ++i) {
        x[i] /= f.D[i];
    }

    // Back substitution: L^T x = z (upper-triangular with unit diagonal).
    for (std::size_t ii = n; ii-- > 0;) {
        f64 s = x[ii];
        for (std::size_t k = ii + 1; k < n; ++k) {
            s -= f.L.at(k, ii) * x[k];
        }
        x[ii] = s;  // L^T(ii,ii) == 1
    }

    return x;
}

}  // namespace aleph::linalg::sparse
