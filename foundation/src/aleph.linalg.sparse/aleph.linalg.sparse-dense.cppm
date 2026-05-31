module;
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

private:
    std::size_t      rows_{0};
    std::size_t      cols_{0};
    std::vector<f64> data_{};
};

}  // namespace aleph::linalg::sparse
