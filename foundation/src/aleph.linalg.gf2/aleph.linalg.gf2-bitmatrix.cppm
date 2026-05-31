module;
#include <cstddef>
#include <utility>
#include <vector>

export module aleph.linalg.gf2:bitmatrix;

import :bitvec;

export namespace aleph::linalg::gf2 {

// Dense matrix over GF(2), stored as `rows` BitVec rows each of width `cols`.
// All arithmetic is mod 2; addition is xor.
class BitMatrix {
public:
    BitMatrix() = default;

    // All-zero `r` x `c` matrix.
    BitMatrix(std::size_t r, std::size_t c)
        : rows_count_(r), cols_count_(c), rows_(r, BitVec(c)) {}

    std::size_t rows() const noexcept { return rows_count_; }
    std::size_t cols() const noexcept { return cols_count_; }

    bool at(std::size_t r, std::size_t c) const noexcept { return rows_[r].get(c); }
    void set(std::size_t r, std::size_t c, bool value) noexcept { rows_[r].set(c, value); }

    const BitVec& row(std::size_t r) const noexcept { return rows_[r]; }

    // dst <- dst xor src (GF(2) row addition).
    void row_xor(std::size_t dst, std::size_t src) noexcept { rows_[dst] ^= rows_[src]; }

    // n x n identity.
    static BitMatrix identity(std::size_t n) {
        BitMatrix m(n, n);
        for (std::size_t i = 0; i < n; ++i) m.set(i, i, true);
        return m;
    }

    // cols x rows transpose.
    BitMatrix transpose() const {
        BitMatrix t(cols_count_, rows_count_);
        for (std::size_t r = 0; r < rows_count_; ++r) {
            for (std::size_t c = 0; c < cols_count_; ++c) {
                if (rows_[r].get(c)) t.set(c, r, true);
            }
        }
        return t;
    }

    // Rank over GF(2) via Gaussian elimination. Works on a copy; original
    // matrix is unchanged.
    std::size_t rank() const {
        std::vector<BitVec> m = rows_;  // working copy
        std::size_t pivot_row = 0;
        for (std::size_t col = 0; col < cols_count_ && pivot_row < rows_count_; ++col) {
            // Find a row at or below pivot_row with a 1 in this column.
            std::size_t sel = rows_count_;
            for (std::size_t r = pivot_row; r < rows_count_; ++r) {
                if (m[r].get(col)) { sel = r; break; }
            }
            if (sel == rows_count_) continue;  // no pivot in this column
            std::swap(m[pivot_row], m[sel]);
            // Eliminate this column from every other row.
            for (std::size_t r = 0; r < rows_count_; ++r) {
                if (r != pivot_row && m[r].get(col)) m[r] ^= m[pivot_row];
            }
            ++pivot_row;
        }
        return pivot_row;
    }

    // Basis of the right null space (kernel) over GF(2): all x (cols-wide)
    // with M x = 0. Returns dim = cols - rank() basis vectors.
    //
    // Method: reduced row echelon form; pivot columns are determined, free
    // columns are independent. For each free column f, the basis vector has a 1
    // at f and, for each pivot row p (pivot column cp), bit cp = R[p][f].
    std::vector<BitVec> null_space() const {
        std::vector<BitVec> m = rows_;  // working copy, reduced in place

        // pivot_col_of_row[i] = column index of the pivot in reduced row i,
        // or cols_count_ (sentinel) if row i has no pivot.
        std::vector<std::size_t> pivot_col_of_row(rows_count_, cols_count_);
        std::vector<bool> is_pivot_col(cols_count_, false);

        std::size_t pivot_row = 0;
        for (std::size_t col = 0; col < cols_count_ && pivot_row < rows_count_; ++col) {
            std::size_t sel = rows_count_;
            for (std::size_t r = pivot_row; r < rows_count_; ++r) {
                if (m[r].get(col)) { sel = r; break; }
            }
            if (sel == rows_count_) continue;
            std::swap(m[pivot_row], m[sel]);
            for (std::size_t r = 0; r < rows_count_; ++r) {
                if (r != pivot_row && m[r].get(col)) m[r] ^= m[pivot_row];
            }
            pivot_col_of_row[pivot_row] = col;
            is_pivot_col[col] = true;
            ++pivot_row;
        }

        std::vector<BitVec> basis;
        for (std::size_t free_col = 0; free_col < cols_count_; ++free_col) {
            if (is_pivot_col[free_col]) continue;
            BitVec x(cols_count_);
            x.set(free_col, true);
            // For each pivot row, the pivot variable is constrained:
            //   x[pivot_col] = sum over free columns f of R[row][f] * x[f].
            // Only this single free column is set, so x[pivot_col] = R[row][free_col].
            for (std::size_t pr = 0; pr < pivot_row; ++pr) {
                const std::size_t pc = pivot_col_of_row[pr];
                if (m[pr].get(free_col)) x.set(pc, true);
            }
            basis.push_back(std::move(x));
        }
        return basis;
    }

private:
    std::size_t         rows_count_{0};
    std::size_t         cols_count_{0};
    std::vector<BitVec> rows_{};
};

}  // namespace aleph::linalg::gf2
