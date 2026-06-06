module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

export module aleph.linalg.sparse:bk_ldlt;

import aleph.math;
import :dense;

// Dense Bunch-Kaufman LDL^T factorization of a symmetric INDEFINITE matrix
// (Bunch & Kaufman 1977; Golub & Van Loan §4.4).
//
// Ported term-for-term from aleph-engine/aleph-audio/src/bk_ldlt.rs.
//
// Computes  P · H · P^T = L · D · L^T  with L unit-lower-triangular and D
// block-diagonal of 1×1 and 2×2 pivots (threshold partial pivoting). Unlike
// the PSD LDLT this handles symmetric-indefinite matrices (e.g. Δ − k²I past
// the spectral gap).
//
// Adaptation notes (vs. the Rust reference):
//   * Rust's `DMatrix::{get,set}` map to the C++ `DMatrix::at(i,j)` accessor.
//   * Rust returns `Result<_, BkError>`; C++ uses std::expected with a bare
//     enum + Info-struct carrying the offending index (mirroring LdltErrorInfo).
//     `solve` returns std::optional, per aleph_flags_isa (no exceptions).
//   * BK_ALPHA = 0.640'388'2 is copied verbatim as a compile-time LITERAL
//     (the truncated (1+√17)/8). It is NOT recomputed via std::sqrt so the
//     pivot decisions match the Rust oracle bit-for-bit.

export namespace aleph::linalg::sparse {

using aleph::math::f64;

// Rust BK_ALPHA literal (truncated (1+√17)/8). DO NOT recompute via std::sqrt.
inline constexpr f64 kBkAlpha = 0.640'388'2;

enum class BkError {
    NotSquare,
    NotSymmetric,
    Singular,  // offending index reported via BkErrorInfo
};

// Error payload carrying the optional pivot index for Singular (Rust
// `Singular(usize)`). Mirrors LdltErrorInfo.
struct BkErrorInfo {
    BkError     kind;
    std::size_t index{0};  // meaningful only for Singular

    [[nodiscard]] friend bool operator==(const BkErrorInfo&, const BkErrorInfo&) = default;
};

// One diagonal block of D. 1×1 is a scalar; 2×2 is a symmetric 2×2 matrix
// [[d11, d12], [d12, d22]] (mirror Rust `enum DBlock { One(f64), Two(f64,f64,f64) }`).
struct DBlock {
    bool is_two{false};
    f64  d11{0.0};  // 1×1 scalar lives here too (the One value)
    f64  d12{0.0};
    f64  d22{0.0};

    [[nodiscard]] static DBlock One(f64 d) { return DBlock{false, d, 0.0, 0.0}; }
    [[nodiscard]] static DBlock Two(f64 a11, f64 a12, f64 a22) {
        return DBlock{true, a11, a12, a22};
    }

    // Block dimension (1 or 2), for the dimension-sum oracles.
    [[nodiscard]] std::size_t size() const noexcept { return is_two ? 2u : 1u; }
};

// P · H · P^T = L · D · L^T with L unit-lower-triangular and D block-diagonal.
struct BkLdlt {
    std::size_t              n{0};
    DMatrix                  l;     // unit-lower-triangular, n x n
    std::vector<DBlock>      d;     // block-diagonal of D
    std::vector<std::size_t> perm;  // permutation (Rust Vec<usize>)

    // Factor symmetric matrix via Bunch-Kaufman with mixed 1×1 and 2×2 pivots
    // and the standard threshold partial-pivoting rule (port BkLdlt::factorize).
    [[nodiscard]] static std::expected<BkLdlt, BkErrorInfo>
    factorize(const DMatrix& matrix) {
        if (matrix.rows() != matrix.cols()) {
            return std::unexpected(BkErrorInfo{BkError::NotSquare});
        }
        const std::size_t size = matrix.rows();
        const DMatrix transposed = matrix.transpose();
        if (!matrix.approx_eq(transposed, 1e-7)) {
            return std::unexpected(BkErrorInfo{BkError::NotSymmetric});
        }

        DMatrix work = matrix;  // work-copy
        DMatrix l = DMatrix::identity(size);
        std::vector<DBlock> d;
        d.reserve(size);
        std::vector<std::size_t> perm(size);
        for (std::size_t i = 0; i < size; ++i) {
            perm[i] = i;
        }

        std::size_t j = 0;
        while (j < size) {
            const auto [block_size, swap_to] = choose_pivot(work, j);
            const std::size_t target = (block_size == 2) ? (j + 1) : j;
            if (swap_to != target) {
                swap_rows_cols(work, target, swap_to);
                swap_rows_cols(l, target, swap_to);
                std::swap(perm[target], perm[swap_to]);
            }
            if (block_size == 1) {
                const f64 pivot = work.at(j, j);
                if (std::abs(pivot) < 1e-14) {
                    return std::unexpected(BkErrorInfo{BkError::Singular, j});
                }
                d.push_back(DBlock::One(pivot));
                for (std::size_t i = j + 1; i < size; ++i) {
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
                j += 1;
            } else {
                // 2×2 pivot at (j, j+1).
                const f64 d11 = work.at(j, j);
                const f64 d12 = work.at(j, j + 1);
                const f64 d22 = work.at(j + 1, j + 1);
                const f64 det = d11 * d22 - d12 * d12;
                if (std::abs(det) < 1e-14) {
                    return std::unexpected(BkErrorInfo{BkError::Singular, j});
                }
                d.push_back(DBlock::Two(d11, d12, d22));
                const f64 inv_det = 1.0 / det;
                for (std::size_t i = j + 2; i < size; ++i) {
                    const f64 a = work.at(i, j);
                    const f64 b = work.at(i, j + 1);
                    const f64 l1 = inv_det * (d22 * a - d12 * b);
                    const f64 l2 = inv_det * (-d12 * a + d11 * b);
                    l.at(i, j) = l1;
                    l.at(i, j + 1) = l2;
                    for (std::size_t k = j + 2; k <= i; ++k) {
                        const f64 updated =
                            work.at(i, k) - l1 * work.at(j, k) - l2 * work.at(j + 1, k);
                        work.at(i, k) = updated;
                        if (k != i) {
                            work.at(k, i) = updated;
                        }
                    }
                }
                j += 2;
            }
        }
        return BkLdlt{size, std::move(l), std::move(d), std::move(perm)};
    }

    // Solve H · x = b via P · H · P^T = L · D · L^T (port BkLdlt::solve).
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(const std::vector<f64>& b) const {
        if (b.size() != n) {
            return std::nullopt;
        }
        // y = P · b.
        std::vector<f64> y(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = b[perm[i]];
        }
        // L · z = y (forward substitution).
        std::vector<f64> z = y;
        for (std::size_t i = 0; i < n; ++i) {
            f64 s = z[i];
            for (std::size_t k = 0; k < i; ++k) {
                s -= l.at(i, k) * z[k];
            }
            z[i] = s;
        }
        // D · w = z (block-diagonal).
        std::vector<f64> w(n, 0.0);
        std::size_t idx = 0;
        for (const DBlock& block : d) {
            if (!block.is_two) {
                const f64 dd = block.d11;
                if (std::abs(dd) < 1e-14) {
                    return std::nullopt;
                }
                w[idx] = z[idx] / dd;
                idx += 1;
            } else {
                const f64 d11 = block.d11;
                const f64 d12 = block.d12;
                const f64 d22 = block.d22;
                const f64 det = d11 * d22 - d12 * d12;
                if (std::abs(det) < 1e-14) {
                    return std::nullopt;
                }
                const f64 inv_det = 1.0 / det;
                const f64 a = z[idx];
                const f64 b2 = z[idx + 1];
                w[idx] = inv_det * (d22 * a - d12 * b2);
                w[idx + 1] = inv_det * (-d12 * a + d11 * b2);
                idx += 2;
            }
        }
        // L^T · u = w (backward substitution).
        std::vector<f64> u = w;
        for (std::size_t i = n; i-- > 0;) {
            f64 s = u[i];
            for (std::size_t k = i + 1; k < n; ++k) {
                s -= l.at(k, i) * u[k];
            }
            u[i] = s;
        }
        // x = P^T · u.
        std::vector<f64> x(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            x[perm[i]] = u[i];
        }
        return x;
    }

private:
    // Bunch-Kaufman pivot rule. Returns (block_size, swap_to) (port choose_pivot).
    [[nodiscard]] static std::pair<std::size_t, std::size_t>
    choose_pivot(const DMatrix& work, std::size_t j) {
        const std::size_t n = work.rows();
        if (j == n - 1) {
            return {1u, j};
        }
        f64 omega1 = 0.0;
        std::size_t r = j + 1;
        for (std::size_t i = j + 1; i < n; ++i) {
            const f64 v = std::abs(work.at(i, j));
            if (v > omega1) {
                omega1 = v;
                r = i;
            }
        }
        if (omega1 < 1e-14) {
            return {1u, j};
        }
        const f64 ajj = std::abs(work.at(j, j));
        if (ajj >= kBkAlpha * omega1) {
            return {1u, j};
        }
        f64 omega_r = 0.0;
        for (std::size_t i = j; i < n; ++i) {
            if (i == r) {
                continue;
            }
            omega_r = std::max(omega_r, std::abs(work.at(i, r)));
        }
        if (ajj * omega_r >= kBkAlpha * omega1 * omega1) {
            return {1u, j};
        }
        const f64 arr = std::abs(work.at(r, r));
        if (arr >= kBkAlpha * omega_r) {
            return {1u, r};
        }
        return {2u, r};
    }

    // Swap rows a,b then columns a,b of m in place (port swap_rows_cols).
    static void swap_rows_cols(DMatrix& m, std::size_t a, std::size_t b) {
        if (a == b) {
            return;
        }
        const std::size_t n = m.rows();
        for (std::size_t k = 0; k < n; ++k) {
            const f64 va = m.at(a, k);
            const f64 vb = m.at(b, k);
            m.at(a, k) = vb;
            m.at(b, k) = va;
        }
        for (std::size_t k = 0; k < n; ++k) {
            const f64 va = m.at(k, a);
            const f64 vb = m.at(k, b);
            m.at(k, a) = vb;
            m.at(k, b) = va;
        }
    }
};

}  // namespace aleph::linalg::sparse
