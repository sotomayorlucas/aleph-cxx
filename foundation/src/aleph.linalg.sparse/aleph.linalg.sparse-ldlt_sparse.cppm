module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <vector>

export module aleph.linalg.sparse:ldlt_sparse;

import aleph.math;
import :csr;
import :ldlt;

// Sparse LDL^T factorization (Davis 2006, Ch. 4 + 11).
//
// Ported from aleph-engine/aleph-flow/src/sparse_ldlt.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust's `BTreeSet<usize>` sorted merge maps to `std::set<std::size_t>`.
//   * Rust returns `Result<_, LdltError>`; C++ uses std::expected with the
//     LdltErrorInfo payload (carrying the NotPsd pivot index). solve() returns
//     std::optional, None when b is not in the range of A.
//   * Elimination-tree parents are std::optional<std::size_t> (Rust
//     Option<usize>); None == a root.
//   * The sparse pivot thresholds are copied verbatim from the Rust:
//     a pivot < -1e-9 => NotPsd; |pivot| < 1e-14 => kernel direction; the
//     solve diagonal/range guards use 1e-14 / 1e-9.

export namespace aleph::linalg::sparse {

using aleph::math::f64;

// Forward declarations (non-inline free functions need a prior declaration for
// the -Wmissing-declarations zero-warning gate).
[[nodiscard]] std::vector<std::optional<std::size_t>>
elimination_tree(const CsrMatrix& a);
[[nodiscard]] std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
symbolic_factor(const CsrMatrix& a,
                std::span<const std::optional<std::size_t>> parent);

// Elimination tree of a symmetric matrix A. parent[j] is the elim-tree parent
// of column j; nullopt indicates a root. Liu's algorithm (Davis 4.2): for each
// column j, walk the row indices of A's strictly-lower triangle; for each i,
// follow the ancestor chain until reaching nullopt or a column > j; mark the
// last visited as parent[*] = j.
[[nodiscard]] std::vector<std::optional<std::size_t>>
elimination_tree(const CsrMatrix& a) {
    const std::size_t n = a.rows();
    std::vector<std::optional<std::size_t>> parent(n, std::nullopt);
    std::vector<std::optional<std::size_t>> ancestor(n, std::nullopt);
    const auto& row_ptr = a.row_ptr();
    const auto& col_idx = a.col_idx();
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t k = row_ptr[j]; k < row_ptr[j + 1]; ++k) {
            std::size_t i = col_idx[k];
            while (i < j) {
                const std::optional<std::size_t> next = ancestor[i];
                ancestor[i] = j;
                if (!next.has_value()) {
                    parent[i] = j;
                    break;
                }
                i = *next;
            }
        }
    }
    return parent;
}

// Symbolic factorization: determine the non-zero pattern of L (strict lower
// triangle, unit diagonal implicit). Returns (l_col_ptr, l_row_idx) in CSC
// layout. Column-by-column fill propagation (Davis 4.3): for each column j,
// L[:,j] starts with {i : A[i,j] != 0, i > j}, then for every child k of j in
// the elimination tree whose L column contains j, all entries of L[:,k] above
// row j are merged in.
[[nodiscard]] std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
symbolic_factor(const CsrMatrix& a,
                std::span<const std::optional<std::size_t>> parent) {
    const std::size_t n = a.rows();
    const auto& row_ptr = a.row_ptr();
    const auto& col_idx = a.col_idx();

    // Build children lists from the elimination tree.
    std::vector<std::vector<std::size_t>> children(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (parent[k].has_value()) {
            children[*parent[k]].push_back(k);
        }
    }

    // Build CSC of A's strict lower triangle for O(1) column access.
    std::vector<std::size_t> col_count(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = row_ptr[i]; k < row_ptr[i + 1]; ++k) {
            const std::size_t c = col_idx[k];
            if (i > c) {
                ++col_count[c];
            }
        }
    }
    std::vector<std::size_t> a_csc_col_ptr(n + 1, 0);
    for (std::size_t c = 0; c < n; ++c) {
        a_csc_col_ptr[c + 1] = a_csc_col_ptr[c] + col_count[c];
    }
    const std::size_t total = a_csc_col_ptr[n];
    std::vector<std::size_t> a_csc_row_idx(total, 0);
    std::vector<std::size_t> fill_ptr(a_csc_col_ptr.begin(),
                                      a_csc_col_ptr.begin() + static_cast<std::ptrdiff_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = row_ptr[i]; k < row_ptr[i + 1]; ++k) {
            const std::size_t c = col_idx[k];
            if (i > c) {
                a_csc_row_idx[fill_ptr[c]] = i;
                ++fill_ptr[c];
            }
        }
    }

    // l_col[j] accumulates the row indices of L[:,j] in sorted order.
    std::vector<std::vector<std::size_t>> l_col(n);
    for (std::size_t j = 0; j < n; ++j) {
        // Seed: non-zeros from A's strict lower triangle, column j.
        std::set<std::size_t> col_j;
        for (std::size_t p = a_csc_col_ptr[j]; p < a_csc_col_ptr[j + 1]; ++p) {
            col_j.insert(a_csc_row_idx[p]);
        }
        // Fill propagation via elim-tree children of j: each child k with
        // j in L[:,k] contributes its entries > j to L[:,j].
        for (const std::size_t k : children[j]) {
            const auto& kcol = l_col[k];
            if (std::binary_search(kcol.begin(), kcol.end(), j)) {
                for (const std::size_t r : kcol) {
                    if (r > j) {
                        col_j.insert(r);
                    }
                }
            }
        }
        l_col[j].assign(col_j.begin(), col_j.end());
    }

    std::vector<std::size_t> l_col_ptr;
    l_col_ptr.reserve(n + 1);
    l_col_ptr.push_back(0);
    std::vector<std::size_t> l_row_idx;
    for (const auto& col : l_col) {
        l_row_idx.insert(l_row_idx.end(), col.begin(), col.end());
        l_col_ptr.push_back(l_row_idx.size());
    }
    return {std::move(l_col_ptr), std::move(l_row_idx)};
}

// Sparse LDL^T factor of a symmetric PSD matrix.
struct SparseLdlt {
    std::size_t              n{0};
    std::vector<std::size_t> l_col_ptr;
    std::vector<std::size_t> l_row_idx;
    std::vector<f64>         l_values;
    std::vector<f64>         d;

    // Factor A = L D L^T for symmetric PSD A (port SparseLdlt::factorize).
    [[nodiscard]] static std::expected<SparseLdlt, LdltErrorInfo>
    factorize(const CsrMatrix& a) {
        if (a.rows() != a.cols()) {
            return std::unexpected(LdltErrorInfo{LdltError::NotSquare});
        }
        if (!a.is_symmetric(1e-7)) {
            return std::unexpected(LdltErrorInfo{LdltError::NotSymmetric});
        }
        const std::size_t n = a.rows();
        const auto parent = elimination_tree(a);
        auto [l_col_ptr, l_row_idx] =
            symbolic_factor(a, std::span<const std::optional<std::size_t>>(parent));
        const std::size_t nnz_l = l_row_idx.size();
        std::vector<f64> l_values(nnz_l, 0.0);
        std::vector<f64> d(n, 0.0);
        std::vector<f64> work(n, 0.0);

        const auto& row_ptr = a.row_ptr();
        const auto& col_idx = a.col_idx();
        const auto& values  = a.values();

        for (std::size_t j = 0; j < n; ++j) {
            // Zero out work[] entries we'll touch this iteration.
            work[j] = 0.0;
            for (std::size_t off = l_col_ptr[j]; off < l_col_ptr[j + 1]; ++off) {
                work[l_row_idx[off]] = 0.0;
            }

            // Scatter A's column j (lower triangle) into work.
            for (std::size_t k = row_ptr[j]; k < row_ptr[j + 1]; ++k) {
                if (col_idx[k] == j) {
                    work[j] = values[k];
                }
            }
            // A[i, j] for i > j — by symmetry equals A[j, i].
            for (std::size_t off = l_col_ptr[j]; off < l_col_ptr[j + 1]; ++off) {
                const std::size_t i = l_row_idx[off];
                work[i] = a.get(j, i);
            }

            // Subtract contributions of prior columns k < j with L[j, k] != 0.
            for (std::size_t k = 0; k < j; ++k) {
                const std::size_t kl_lo = l_col_ptr[k];
                const std::size_t kl_hi = l_col_ptr[k + 1];
                const auto first = l_row_idx.begin() + static_cast<std::ptrdiff_t>(kl_lo);
                const auto last  = l_row_idx.begin() + static_cast<std::ptrdiff_t>(kl_hi);
                const auto it = std::lower_bound(first, last, j);
                if (it != last && *it == j) {
                    const std::size_t p = static_cast<std::size_t>(it - first);
                    const f64 l_jk  = l_values[kl_lo + p];
                    const f64 scale = l_jk * d[k];
                    for (std::size_t off = 0; off < (kl_hi - kl_lo); ++off) {
                        const std::size_t i = l_row_idx[kl_lo + off];
                        if (i < j) {
                            continue;
                        }
                        const f64 l_ik = l_values[kl_lo + off];
                        work[i] -= scale * l_ik;
                    }
                }
            }

            // Pivot D[j] = work[j].
            const f64 pivot = work[j];
            if (pivot < -1e-9) {
                return std::unexpected(LdltErrorInfo{LdltError::NotPsd, j});
            }
            d[j] = pivot;
            if (std::abs(pivot) < 1e-14) {
                // Zero pivot — kernel direction.
                for (std::size_t off = l_col_ptr[j]; off < l_col_ptr[j + 1]; ++off) {
                    l_values[off] = 0.0;
                }
                continue;
            }
            // L[i, j] = work[i] / pivot for i in pattern.
            for (std::size_t off = l_col_ptr[j]; off < l_col_ptr[j + 1]; ++off) {
                const std::size_t i = l_row_idx[off];
                l_values[off] = work[i] / pivot;
            }
        }

        return SparseLdlt{n, std::move(l_col_ptr), std::move(l_row_idx),
                          std::move(l_values), std::move(d)};
    }

    // Solve A x = b via L y = b, D z = y, L^T x = z (port SparseLdlt::solve).
    // Returns nullopt when b is not in the range of A.
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(std::span<const f64> b) const {
        if (b.size() != n) {
            return std::nullopt;
        }
        // Forward-substitute L y = b. L stored as CSC (strict lower, unit diag).
        std::vector<f64> y(b.begin(), b.end());
        for (std::size_t j = 0; j < n; ++j) {
            for (std::size_t off = l_col_ptr[j]; off < l_col_ptr[j + 1]; ++off) {
                const std::size_t i = l_row_idx[off];
                y[i] -= l_values[off] * y[j];
            }
        }
        // D z = y.
        std::vector<f64> z(n, 0.0);
        for (std::size_t j = 0; j < n; ++j) {
            if (std::abs(d[j]) < 1e-14) {
                if (std::abs(y[j]) > 1e-9) {
                    return std::nullopt;
                }
                z[j] = 0.0;
            } else {
                z[j] = y[j] / d[j];
            }
        }
        // Backward-substitute L^T x = z. Walk j from n-1 down.
        std::vector<f64> x = std::move(z);
        for (std::size_t jj = n; jj-- > 0;) {
            f64 acc = x[jj];
            for (std::size_t off = l_col_ptr[jj]; off < l_col_ptr[jj + 1]; ++off) {
                const std::size_t i = l_row_idx[off];
                acc -= l_values[off] * x[i];
            }
            x[jj] = acc;
        }
        return x;
    }
};

}  // namespace aleph::linalg::sparse
