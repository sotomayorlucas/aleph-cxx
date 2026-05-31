#include "doctest.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
import aleph.linalg.gf2;

using aleph::linalg::gf2::BitVec;
using aleph::linalg::gf2::BitMatrix;

// Independent oracle: M * x over GF(2), computed without trusting the impl's
// own null_space machinery. Returns a `rows`-wide BitVec product.
static BitVec mat_vec_mul(const BitMatrix& m, const BitVec& x) {
    BitVec out(m.rows());
    for (std::size_t r = 0; r < m.rows(); ++r) {
        bool acc = false;
        for (std::size_t c = 0; c < m.cols(); ++c) {
            acc ^= (m.at(r, c) && x.get(c));
        }
        out.set(r, acc);
    }
    return out;
}

TEST_CASE("BitVec: set/get round-trips") {
    BitVec v(130);  // spans 3 words
    CHECK(v.size() == 130);
    CHECK(v.is_zero());
    v.set(0, true);
    v.set(63, true);
    v.set(64, true);
    v.set(129, true);
    CHECK(v.get(0));
    CHECK(v.get(63));
    CHECK(v.get(64));
    CHECK(v.get(129));
    CHECK_FALSE(v.get(1));
    CHECK_FALSE(v.get(62));
    CHECK_FALSE(v.get(65));
    CHECK_FALSE(v.is_zero());
    v.set(63, false);
    CHECK_FALSE(v.get(63));
}

TEST_CASE("BitVec: popcount counts set bits") {
    BitVec v(200);
    CHECK(v.popcount() == 0);
    const std::size_t idx[] = {0, 1, 64, 100, 150, 199};
    for (std::size_t i : idx) v.set(i, true);
    CHECK(v.popcount() == 6);
    v.flip(0);  // clears bit 0
    CHECK(v.popcount() == 5);
    v.flip(7);  // sets a fresh bit
    CHECK(v.popcount() == 6);
}

TEST_CASE("BitVec: xor of a vector with itself is all-zero") {
    BitVec v(95);
    for (std::size_t i = 0; i < 95; i += 3) v.set(i, true);
    CHECK_FALSE(v.is_zero());
    BitVec copy = v;
    v ^= copy;
    CHECK(v.is_zero());
    CHECK(v == BitVec(95));
}

TEST_CASE("rank(identity(n)) == n for n in {1,4,7}") {
    CHECK(BitMatrix::identity(1).rank() == 1);
    CHECK(BitMatrix::identity(4).rank() == 4);
    CHECK(BitMatrix::identity(7).rank() == 7);
}

TEST_CASE("rank(all-zero r x c) == 0") {
    CHECK(BitMatrix(3, 5).rank() == 0);
    CHECK(BitMatrix(8, 8).rank() == 0);
    CHECK(BitMatrix(1, 1).rank() == 0);
}

TEST_CASE("known singular 3x3 over GF(2) has rank 2") {
    // rows {110, 011, 101}; row3 = row1 xor row2, so rank is 2.
    BitMatrix m(3, 3);
    // row 0: 1 1 0
    m.set(0, 0, true); m.set(0, 1, true);
    // row 1: 0 1 1
    m.set(1, 1, true); m.set(1, 2, true);
    // row 2: 1 0 1
    m.set(2, 0, true); m.set(2, 2, true);
    CHECK(m.rank() == 2);

    // null space dimension must equal cols - rank = 3 - 2 = 1.
    auto ker = m.null_space();
    CHECK(ker.size() == 1);
    for (const auto& k : ker) {
        CHECK(mat_vec_mul(m, k).is_zero());
        CHECK_FALSE(k.is_zero());  // a genuine kernel vector
    }
}

TEST_CASE("transpose round-trips and preserves rank") {
    BitMatrix m(2, 3);
    m.set(0, 0, true); m.set(0, 2, true);
    m.set(1, 1, true);
    BitMatrix t = m.transpose();
    CHECK(t.rows() == 3);
    CHECK(t.cols() == 2);
    CHECK(t.at(0, 0));
    CHECK(t.at(2, 0));
    CHECK(t.at(1, 1));
    CHECK(t.rank() == m.rank());
    // double transpose is the original
    BitMatrix tt = t.transpose();
    CHECK(tt.rows() == m.rows());
    CHECK(tt.cols() == m.cols());
    for (std::size_t r = 0; r < m.rows(); ++r)
        for (std::size_t c = 0; c < m.cols(); ++c)
            CHECK(tt.at(r, c) == m.at(r, c));
}

TEST_CASE("null_space: dim == cols - rank and M*k == 0 for several matrices") {
    // A spread of deterministic-but-varied matrices via a small LCG fill.
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next_bit = [&state]() -> bool {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return (state >> 33) & 1u;
    };

    const std::pair<std::size_t, std::size_t> shapes[] = {
        {5, 7}, {7, 5}, {6, 6}, {3, 9}, {10, 4},
    };

    for (auto [rows, cols] : shapes) {
        for (int trial = 0; trial < 8; ++trial) {
            BitMatrix m(rows, cols);
            for (std::size_t r = 0; r < rows; ++r)
                for (std::size_t c = 0; c < cols; ++c)
                    if (next_bit()) m.set(r, c, true);

            const std::size_t rk = m.rank();
            auto ker = m.null_space();

            // dimension oracle: nullity = cols - rank
            CHECK(ker.size() == cols - rk);

            // every basis vector is annihilated by M
            for (const auto& k : ker) {
                CHECK(k.size() == cols);
                CHECK(mat_vec_mul(m, k).is_zero());
            }
        }
    }
}

TEST_CASE("null_space of full-rank square matrix is empty") {
    auto ker = BitMatrix::identity(5).null_space();
    CHECK(ker.empty());
}

TEST_CASE("null_space of all-zero matrix spans all of cols") {
    BitMatrix m(2, 4);  // rank 0, nullity 4
    auto ker = m.null_space();
    CHECK(ker.size() == 4);
    for (const auto& k : ker) CHECK(mat_vec_mul(m, k).is_zero());
}
