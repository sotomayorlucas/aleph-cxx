module;
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

export module aleph.linalg.sparse:dense;

import aleph.math;

export namespace aleph::linalg::sparse {

using aleph::math::f64;

// Row-major dense matrix over f64. Element (i, j) lives at data_[i*cols_ + j].
class DMatrix {
public:
    DMatrix() = default;

    // rows x cols matrix, all entries initialized to fill (default 0).
    DMatrix(std::size_t rows, std::size_t cols, f64 fill = 0.0)
        : rows_(rows), cols_(cols), data_(rows * cols, fill) {}

    // rows x cols matrix of zeros (port linalg_f64::DMatrix::zeros).
    [[nodiscard]] static DMatrix zeros(std::size_t rows, std::size_t cols) {
        return DMatrix(rows, cols, 0.0);
    }

    // n x n identity (port linalg_f64::DMatrix::identity).
    [[nodiscard]] static DMatrix identity(std::size_t n) {
        DMatrix m(n, n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            m.data_[i * n + i] = 1.0;
        }
        return m;
    }

    // Build from a list of rows (port linalg_f64::DMatrix::from_rows).
    // All rows must have equal length; ragged input yields an empty matrix.
    [[nodiscard]] static DMatrix from_rows(const std::vector<std::vector<f64>>& rows) {
        const std::size_t n_rows = rows.size();
        const std::size_t n_cols = (n_rows == 0) ? 0 : rows[0].size();
        for (const auto& row : rows) {
            if (row.size() != n_cols) {
                return DMatrix();  // ragged rows — degenerate to empty
            }
        }
        DMatrix m(n_rows, n_cols, 0.0);
        for (std::size_t i = 0; i < n_rows; ++i) {
            for (std::size_t j = 0; j < n_cols; ++j) {
                m.data_[i * n_cols + j] = rows[i][j];
            }
        }
        return m;
    }

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

    [[nodiscard]] f64& at(std::size_t i, std::size_t j) noexcept {
        return data_[i * cols_ + j];
    }
    [[nodiscard]] const f64& at(std::size_t i, std::size_t j) const noexcept {
        return data_[i * cols_ + j];
    }

    // y = A * x, where x has cols_ entries; result has rows_ entries.
    [[nodiscard]] std::vector<f64> matvec(std::span<const f64> x) const {
        std::vector<f64> y(rows_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            f64 acc = 0.0;
            const std::size_t base = i * cols_;
            for (std::size_t j = 0; j < cols_; ++j) {
                acc += data_[base + j] * x[j];
            }
            y[i] = acc;
        }
        return y;
    }

    // C = A * B. A is rows_ x cols_, B is cols_ x B.cols(); C is rows_ x B.cols().
    [[nodiscard]] DMatrix matmul(const DMatrix& b) const {
        DMatrix c(rows_, b.cols_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t k = 0; k < cols_; ++k) {
                const f64 a_ik = data_[i * cols_ + k];
                if (a_ik == 0.0) continue;
                const std::size_t b_base = k * b.cols_;
                const std::size_t c_base = i * b.cols_;
                for (std::size_t j = 0; j < b.cols_; ++j) {
                    c.data_[c_base + j] += a_ik * b.data_[b_base + j];
                }
            }
        }
        return c;
    }

    // A^T: cols_ x rows_.
    [[nodiscard]] DMatrix transpose() const {
        DMatrix t(cols_, rows_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t j = 0; j < cols_; ++j) {
                t.data_[j * rows_ + i] = data_[i * cols_ + j];
            }
        }
        return t;
    }

    // Element-wise sum (port linalg_f64::DMatrix::add). Same shape assumed.
    [[nodiscard]] DMatrix add(const DMatrix& other) const {
        DMatrix out(rows_, cols_, 0.0);
        for (std::size_t k = 0; k < data_.size(); ++k) {
            out.data_[k] = data_[k] + other.data_[k];
        }
        return out;
    }

    // Scalar multiple (port linalg_f64::DMatrix::scale).
    [[nodiscard]] DMatrix scale(f64 alpha) const {
        DMatrix out(*this);
        for (auto& v : out.data_) {
            v *= alpha;
        }
        return out;
    }

    // True when shapes match and every entry agrees within eps
    // (port linalg_f64::DMatrix::approx_eq).
    [[nodiscard]] bool approx_eq(const DMatrix& other, f64 eps) const {
        if (rows_ != other.rows_ || cols_ != other.cols_) {
            return false;
        }
        for (std::size_t k = 0; k < data_.size(); ++k) {
            if (std::abs(data_[k] - other.data_[k]) > eps) {
                return false;
            }
        }
        return true;
    }

    // Frobenius norm of (this - other) (port linalg_f64::DMatrix::frobenius_norm_diff).
    [[nodiscard]] f64 frobenius_norm_diff(const DMatrix& other) const {
        f64 s = 0.0;
        for (std::size_t k = 0; k < data_.size(); ++k) {
            const f64 d = data_[k] - other.data_[k];
            s += d * d;
        }
        return std::sqrt(s);
    }

private:
    std::size_t      rows_{0};
    std::size_t      cols_{0};
    std::vector<f64> data_{};
};

}  // namespace aleph::linalg::sparse
