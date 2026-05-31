module;
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

    // Build from a dense matrix, dropping exact zero entries.
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
