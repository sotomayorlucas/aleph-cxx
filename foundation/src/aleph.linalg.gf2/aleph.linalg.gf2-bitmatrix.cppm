module;
#include <bit>
#include <cstddef>
#include <span>
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

    // Kernel basis: alias for null_space(). Basis of all x (cols-wide) with
    // M x = 0; dimension cols - rank().
    std::vector<BitVec> kernel_basis() const { return null_space(); }

    // Matrix-vector product M*x over GF(2). `x` has width `cols`; the result
    // has width `rows`. result[r] = XOR over c of (M[r][c] AND x[c]).
    // Precondition: x.size() == cols(). No exceptions; ill-sized input is UB.
    BitVec apply(const BitVec& x) const {
        BitVec out(rows_count_);
        for (std::size_t r = 0; r < rows_count_; ++r) {
            // Row dot x over GF(2): xor of bits where both row and x are set.
            const BitVec& row_r = rows_[r];
            // Dot product over GF(2) = parity of the AND of the two bit vectors.
            int ones = 0;
            const std::size_t wc = row_r.word_count();
            for (std::size_t k = 0; k < wc; ++k) {
                ones += std::popcount(row_r.word(k) & x.word(k));
            }
            out.set(r, (ones & 1) != 0);
        }
        return out;
    }

    // Matrix product this * other over GF(2). Result is rows() x other.cols().
    // Precondition: cols() == other.rows(). No exceptions; mismatch is UB.
    BitMatrix mul(const BitMatrix& other) const {
        BitMatrix out(rows_count_, other.cols_count_);
        for (std::size_t r = 0; r < rows_count_; ++r) {
            const BitVec& row_r = rows_[r];
            for (std::size_t k = 0; k < cols_count_; ++k) {
                // For each set entry M[r][k], add (xor) row k of `other`.
                if (row_r.get(k)) out.rows_[r] ^= other.rows_[k];
            }
        }
        return out;
    }

    // True iff every entry is zero.
    bool is_zero() const noexcept {
        for (const BitVec& r : rows_) {
            if (!r.is_zero()) return false;
        }
        return true;
    }

    // Build a matrix whose column c is cols[c]. The result is
    // `nrows` x cols.size(). Each input vector contributes its bits 0..nrows-1
    // down the corresponding column. Precondition: every column has size
    // >= nrows. No exceptions.
    static BitMatrix from_cols(std::span<const BitVec> cols, std::size_t nrows) {
        BitMatrix m(nrows, cols.size());
        for (std::size_t c = 0; c < cols.size(); ++c) {
            for (std::size_t r = 0; r < nrows; ++r) {
                if (cols[c].get(r)) m.set(r, c, true);
            }
        }
        return m;
    }

    // Basis of the column space im(M) over GF(2): the original columns of M
    // that become pivot columns after row reduction. Each returned vector has
    // width `rows`. Size equals rank().
    std::vector<BitVec> image_basis() const {
        std::vector<BitVec> m = rows_;  // working copy, reduced in place

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
            is_pivot_col[col] = true;
            ++pivot_row;
        }

        std::vector<BitVec> basis;
        for (std::size_t c = 0; c < cols_count_; ++c) {
            if (!is_pivot_col[c]) continue;
            // Emit the original (unreduced) column c as a rows-wide vector.
            BitVec col(rows_count_);
            for (std::size_t r = 0; r < rows_count_; ++r) {
                if (rows_[r].get(c)) col.set(r, true);
            }
            basis.push_back(std::move(col));
        }
        return basis;
    }

    // Reduce `v` modulo the span of `image` over GF(2). `image` is a set of
    // rows-wide vectors (e.g. from image_basis()); the result is the residue
    // v + span(image), reduced by Gaussian elimination. The residue is zero
    // iff v lies in span(image). `image` need not be reduced or independent.
    // No exceptions.
    BitVec reduce_modulo_image(const BitVec& v, std::span<const BitVec> image) const {
        // Build an echelon set keyed by pivot (lowest set bit) so reduction is
        // independent of any assumed structure in `image`.
        const std::size_t width = v.size();
        std::vector<BitVec> echelon;          // each has a distinct pivot
        std::vector<std::size_t> pivots;       // pivot index of echelon[i]

        auto lowest_set = [width](const BitVec& b) -> std::size_t {
            for (std::size_t i = 0; i < width; ++i) {
                if (b.get(i)) return i;
            }
            return width;  // all-zero sentinel
        };

        for (const BitVec& bv : image) {
            BitVec cur = bv;
            std::size_t p = lowest_set(cur);
            while (p < width) {
                // Find an existing echelon vector with the same pivot.
                std::size_t hit = echelon.size();
                for (std::size_t i = 0; i < echelon.size(); ++i) {
                    if (pivots[i] == p) { hit = i; break; }
                }
                if (hit == echelon.size()) break;  // new pivot
                cur ^= echelon[hit];
                p = lowest_set(cur);
            }
            if (p < width) {
                echelon.push_back(std::move(cur));
                pivots.push_back(p);
            }
        }

        // Reduce v by the echelon set.
        BitVec res = v;
        std::size_t p = lowest_set(res);
        while (p < width) {
            std::size_t hit = echelon.size();
            for (std::size_t i = 0; i < echelon.size(); ++i) {
                if (pivots[i] == p) { hit = i; break; }
            }
            if (hit == echelon.size()) break;
            res ^= echelon[hit];
            p = lowest_set(res);
        }
        return res;
    }

private:
    std::size_t         rows_count_{0};
    std::size_t         cols_count_{0};
    std::vector<BitVec> rows_{};
};

}  // namespace aleph::linalg::gf2
