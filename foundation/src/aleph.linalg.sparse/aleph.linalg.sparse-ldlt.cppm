module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.linalg.sparse:ldlt;

import aleph.math;
import :dense;

// Dense LDL^T factorization of a symmetric PSD (possibly SINGULAR) matrix.
//
// Ported from aleph-engine/aleph-flow/src/ldlt.rs.
//
// For a symmetric PSD matrix M we compute M = L D L^T where L is unit
// lower-triangular and D is diagonal with non-negative entries. Unlike a
// strict SPD factorization, ZERO pivots are accepted: they mark kernel
// directions (graph Laplacians are singular — the constant vector lies in the
// kernel). At a zero pivot the corresponding L column below the diagonal is
// left at 0 and elimination continues; this does NOT error.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust's `DMatrix::{get,set}` map to the C++ `DMatrix::at(i,j)` accessor.
//   * Rust returns `Result<_, LdltError>`; C++ uses std::expected. The
//     kernel-aware `solve` returns std::optional (None when b is not in the
//     range of M), per aleph_flags_isa (no exceptions).
//   * PIVOT_EPS = 1e-12, copied verbatim. A pivot < -PIVOT_EPS is a negative
//     pivot => NotPsd(index); |pivot| < PIVOT_EPS is a kernel direction.

export namespace aleph::linalg::sparse {

using aleph::math::f64;

inline constexpr f64 kLdltPivotEps = 1e-12;  // Rust PIVOT_EPS

enum class LdltError {
    NotSquare,
    NotSymmetric,
    NotPsd,         // negative pivot; offending index reported via LdltErrorInfo
    DimMismatch,
};

// Error payload carrying the optional pivot index for NotPsd (Rust
// `NotPsd(usize)`). Kept separate from the bare enum so callers that only
// switch on the kind stay simple.
struct LdltErrorInfo {
    LdltError   kind;
    std::size_t index{0};  // meaningful only for NotPsd

    [[nodiscard]] friend bool operator==(const LdltErrorInfo&, const LdltErrorInfo&) = default;
};

// Dense LDL^T factor of a symmetric PSD matrix:  A = L * diag(D) * L^T,
// with L unit-lower-triangular (L(i,i) == 1).
struct LDLT {
    DMatrix          l;  // unit-lower-triangular, n x n
    std::vector<f64> d;  // diagonal, length n
    std::size_t      n{0};

    // Factorize a symmetric PSD matrix (port LDLT::factorize). Work-copy
    // Gaussian elimination accepting zero pivots as kernel directions.
    [[nodiscard]] static std::expected<LDLT, LdltErrorInfo>
    factorize(const DMatrix& m) {
        if (m.rows() != m.cols()) {
            return std::unexpected(LdltErrorInfo{LdltError::NotSquare});
        }
        const std::size_t n = m.rows();
        const DMatrix t = m.transpose();
        if (!m.approx_eq(t, 1e-7)) {
            return std::unexpected(LdltErrorInfo{LdltError::NotSymmetric});
        }
        DMatrix l = DMatrix::identity(n);
        std::vector<f64> d(n, 0.0);
        DMatrix work = m;  // work-copy

        for (std::size_t j = 0; j < n; ++j) {
            const f64 pivot = work.at(j, j);
            if (pivot < -kLdltPivotEps) {
                return std::unexpected(LdltErrorInfo{LdltError::NotPsd, j});
            }
            d[j] = pivot;
            if (std::abs(pivot) < kLdltPivotEps) {
                // Zero pivot: kernel direction. Zero out the column below.
                for (std::size_t i = j + 1; i < n; ++i) {
                    work.at(i, j) = 0.0;
                    work.at(j, i) = 0.0;
                }
                continue;
            }
            for (std::size_t i = j + 1; i < n; ++i) {
                const f64 factor = work.at(i, j) / pivot;
                l.at(i, j) = factor;
                for (std::size_t k = j + 1; k <= i; ++k) {
                    const f64 updated = work.at(i, k) - factor * work.at(j, k);
                    work.at(i, k) = updated;
                    if (k != i) {
                        work.at(k, i) = updated;
                    }
                }
            }
        }
        return LDLT{std::move(l), std::move(d), n};
    }

    // Reconstruct M = L D L^T (port LDLT::reconstruct).
    [[nodiscard]] DMatrix reconstruct() const {
        DMatrix dlt = DMatrix::zeros(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                dlt.at(i, j) = d[i] * l.at(j, i);
            }
        }
        return l.matmul(dlt);
    }

    // Solve M x = b. Returns nullopt if b is not in the range of M
    // (port LDLT::solve). Kernel guard: where |D[i]| < eps the forward-solve
    // residual z[i] must vanish, else b is out of range.
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(std::span<const f64> b) const {
        if (b.size() != n) {
            return std::nullopt;
        }
        std::vector<f64> z(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            f64 s = b[i];
            for (std::size_t k = 0; k < i; ++k) {
                s -= l.at(i, k) * z[k];
            }
            z[i] = s;
        }
        std::vector<f64> y(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            if (std::abs(d[i]) < kLdltPivotEps) {
                if (std::abs(z[i]) > kLdltPivotEps) {
                    return std::nullopt;
                }
                y[i] = 0.0;
            } else {
                y[i] = z[i] / d[i];
            }
        }
        std::vector<f64> x(n, 0.0);
        for (std::size_t ii = n; ii-- > 0;) {
            f64 s = y[ii];
            for (std::size_t k = ii + 1; k < n; ++k) {
                s -= l.at(k, ii) * x[k];
            }
            x[ii] = s;
        }
        return x;
    }

    // Apply rank-1 update M' = M + alpha * v * v^T. Davis (1999) recursion
    // (port LDLT::rank_1_update). Mutates this factorization in place.
    [[nodiscard]] std::expected<void, LdltErrorInfo>
    rank_1_update(f64 alpha, std::span<const f64> v) {
        if (v.size() != n) {
            return std::unexpected(LdltErrorInfo{LdltError::DimMismatch});
        }
        std::vector<f64> w(v.begin(), v.end());
        f64 alpha_curr = alpha;
        for (std::size_t j = 0; j < n; ++j) {
            const f64 p = d[j] + alpha_curr * w[j] * w[j];
            if (p < -kLdltPivotEps) {
                return std::unexpected(LdltErrorInfo{LdltError::NotPsd, j});
            }
            const f64 beta  = alpha_curr * w[j];
            const f64 new_d = p;
            // alpha_next = alpha * d_j_old / d_j_new (Davis 1999 eq. 13).
            const f64 alpha_next =
                (std::abs(new_d) > kLdltPivotEps) ? alpha_curr * d[j] / new_d : 0.0;
            d[j] = new_d;
            for (std::size_t i = j + 1; i < n; ++i) {
                const f64 lij = l.at(i, j);
                w[i] -= w[j] * lij;
                if (std::abs(new_d) > kLdltPivotEps) {
                    l.at(i, j) = lij + (beta / new_d) * w[i];
                }
            }
            alpha_curr = alpha_next;
        }
        return {};
    }

    // Apply rank-k update M' = M + sum_i alpha_i * v_i * v_i^T. Iterates
    // rank-1 (port LDLT::rank_k_update).
    [[nodiscard]] std::expected<void, LdltErrorInfo>
    rank_k_update(std::span<const f64> alphas,
                  std::span<const std::vector<f64>> cols) {
        if (alphas.size() != cols.size()) {
            return std::unexpected(LdltErrorInfo{LdltError::DimMismatch});
        }
        for (std::size_t i = 0; i < alphas.size(); ++i) {
            auto r = rank_1_update(alphas[i], std::span<const f64>(cols[i]));
            if (!r) {
                return r;
            }
        }
        return {};
    }
};

}  // namespace aleph::linalg::sparse
