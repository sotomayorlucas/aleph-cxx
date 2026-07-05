module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

export module aleph.linalg.sparse:csr;

import aleph.math;
import :dense;

export namespace aleph::linalg::sparse {

using aleph::math::f64;

// Compressed Sparse Row matrix over f64.
//   row_ptr has rows()+1 entries; row i occupies [row_ptr[i], row_ptr[i+1]).
//   col_idx and values are parallel arrays of length nnz().
class CsrMatrix {
public:
    CsrMatrix() = default;

    // Empty 0x0 matrix with the canonical single-element row_ptr {0}
    // (port linalg_f64_sparse::CsrMatrix::empty).
    [[nodiscard]] static CsrMatrix empty() {
        CsrMatrix m;
        m.row_ptr_.assign(1, 0);
        return m;
    }

    // Build directly from CSR parts (no dense detour — the O(V+E) assembly
    // path). Preconditions (caller-guaranteed, checked cheaply): row_ptr has
    // rows+1 entries starting at 0 and ending at col_idx.size(); col_idx and
    // values are parallel; column indices are sorted ascending within each
    // row. Returns an empty matrix on a malformed shape (defensive — the
    // assembly code upholds the contract by construction).
    [[nodiscard]] static CsrMatrix from_parts(std::size_t rows, std::size_t cols,
                                              std::vector<std::size_t> row_ptr,
                                              std::vector<std::size_t> col_idx,
                                              std::vector<f64>         values) {
        if (row_ptr.size() != rows + 1 || col_idx.size() != values.size()
            || row_ptr.front() != 0 || row_ptr.back() != values.size()) {
            return CsrMatrix::empty();
        }
        CsrMatrix m;
        m.rows_    = rows;
        m.cols_    = cols;
        m.row_ptr_ = std::move(row_ptr);
        m.col_idx_ = std::move(col_idx);
        m.values_  = std::move(values);
        return m;
    }

    // Build from a dense matrix, dropping entries with |v| <= eps
    // (port linalg_f64_sparse::CsrMatrix::from_dense_eps).
    [[nodiscard]] static CsrMatrix from_dense_eps(const DMatrix& a, f64 eps) {
        CsrMatrix m;
        m.rows_ = a.rows();
        m.cols_ = a.cols();
        m.row_ptr_.assign(a.rows() + 1, 0);
        for (std::size_t i = 0; i < a.rows(); ++i) {
            for (std::size_t j = 0; j < a.cols(); ++j) {
                const f64 v = a.at(i, j);
                if (std::abs(v) > eps) {
                    m.col_idx_.push_back(j);
                    m.values_.push_back(v);
                }
            }
            m.row_ptr_[i + 1] = m.values_.size();
        }
        return m;
    }

    // Build from a dense matrix, dropping exact zero entries.
    // (The Rust default uses eps=1e-15; this exact-zero drop matches the
    //  established C++ behaviour and round-trips bit-for-bit for the oracles.)
    [[nodiscard]] static CsrMatrix from_dense(const DMatrix& a) {
        CsrMatrix m;
        m.rows_ = a.rows();
        m.cols_ = a.cols();
        m.row_ptr_.assign(a.rows() + 1, 0);
        for (std::size_t i = 0; i < a.rows(); ++i) {
            for (std::size_t j = 0; j < a.cols(); ++j) {
                const f64 v = a.at(i, j);
                if (v != 0.0) {
                    m.col_idx_.push_back(j);
                    m.values_.push_back(v);
                }
            }
            m.row_ptr_[i + 1] = m.values_.size();
        }
        return m;
    }

    // Materialise as a dense matrix (port linalg_f64_sparse::CsrMatrix::to_dense).
    [[nodiscard]] DMatrix to_dense() const {
        DMatrix m(rows_, cols_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t k = row_ptr_[i]; k < row_ptr_[i + 1]; ++k) {
                m.at(i, col_idx_[k]) = values_[k];
            }
        }
        return m;
    }

    // O(log nnz_row) lookup of the value at (row, col) via binary search on
    // the sorted column indices of that row
    // (port linalg_f64_sparse::CsrMatrix::get).
    [[nodiscard]] f64 get(std::size_t row, std::size_t col) const {
        const std::size_t lo = row_ptr_[row];
        const std::size_t hi = row_ptr_[row + 1];
        const auto first = col_idx_.begin() + static_cast<std::ptrdiff_t>(lo);
        const auto last  = col_idx_.begin() + static_cast<std::ptrdiff_t>(hi);
        const auto it = std::lower_bound(first, last, col);
        if (it != last && *it == col) {
            const std::size_t off = static_cast<std::size_t>(it - first);
            return values_[lo + off];
        }
        return 0.0;
    }

    // CSR transpose, O(nnz + n) (port linalg_f64_sparse::CsrMatrix::transpose).
    [[nodiscard]] CsrMatrix transpose() const {
        CsrMatrix t;
        t.rows_ = cols_;
        t.cols_ = rows_;
        std::vector<std::size_t> col_counts(cols_, 0);
        for (const auto c : col_idx_) {
            ++col_counts[c];
        }
        t.row_ptr_.assign(cols_ + 1, 0);
        for (std::size_t c = 0; c < cols_; ++c) {
            t.row_ptr_[c + 1] = t.row_ptr_[c] + col_counts[c];
        }
        t.col_idx_.assign(values_.size(), 0);
        t.values_.assign(values_.size(), 0.0);
        std::vector<std::size_t> next(t.row_ptr_.begin(),
                                      t.row_ptr_.begin() + static_cast<std::ptrdiff_t>(cols_));
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t k = row_ptr_[i]; k < row_ptr_[i + 1]; ++k) {
                const std::size_t c = col_idx_[k];
                const std::size_t dest = next[c];
                t.col_idx_[dest] = i;
                t.values_[dest]  = values_[k];
                ++next[c];
            }
        }
        return t;
    }

    // Symmetry within tolerance (port linalg_f64_sparse::CsrMatrix::is_symmetric).
    [[nodiscard]] bool is_symmetric(f64 eps) const {
        if (rows_ != cols_) {
            return false;
        }
        const CsrMatrix t = transpose();
        if (t.values_.size() != values_.size()) {
            return false;
        }
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t k = row_ptr_[i]; k < row_ptr_[i + 1]; ++k) {
                const std::size_t c = col_idx_[k];
                if (std::abs(values_[k] - t.get(i, c)) > eps) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
    [[nodiscard]] std::size_t nnz() const noexcept { return values_.size(); }

    [[nodiscard]] const std::vector<std::size_t>& row_ptr() const noexcept { return row_ptr_; }
    [[nodiscard]] const std::vector<std::size_t>& col_idx() const noexcept { return col_idx_; }
    [[nodiscard]] const std::vector<f64>&         values()  const noexcept { return values_; }

    // y = A * x, where x has cols() entries; result has rows() entries.
    [[nodiscard]] std::vector<f64> matvec(std::span<const f64> x) const {
        std::vector<f64> y(rows_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            f64 acc = 0.0;
            const std::size_t beg = row_ptr_[i];
            const std::size_t end = row_ptr_[i + 1];
            for (std::size_t k = beg; k < end; ++k) {
                acc += values_[k] * x[col_idx_[k]];
            }
            y[i] = acc;
        }
        return y;
    }

private:
    std::size_t              rows_{0};
    std::size_t              cols_{0};
    std::vector<std::size_t> row_ptr_{};
    std::vector<std::size_t> col_idx_{};
    std::vector<f64>         values_{};
};

}  // namespace aleph::linalg::sparse
