module;
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

export module aleph.linalg.gf2:bitvec;

export namespace aleph::linalg::gf2 {

// Dense bit vector over GF(2) (the field {0,1}, arithmetic mod 2).
// Bits are packed into 64-bit words, least-significant bit first:
// bit i lives in word i/64 at offset i%64.
class BitVec {
public:
    using word_type = std::uint64_t;
    static constexpr std::size_t bits_per_word = 64;

    BitVec() = default;

    // Construct an all-zero vector of `n` bits.
    explicit BitVec(std::size_t n)
        : bits_(n), words_((n + bits_per_word - 1) / bits_per_word, 0) {}

    std::size_t size() const noexcept { return bits_; }

    // Number of backing words (exposed for word-level callers / matrix ops).
    std::size_t word_count() const noexcept { return words_.size(); }

    bool get(std::size_t i) const noexcept {
        return (words_[i / bits_per_word] >> (i % bits_per_word)) & word_type{1};
    }

    void set(std::size_t i, bool value) noexcept {
        const word_type mask = word_type{1} << (i % bits_per_word);
        word_type& w = words_[i / bits_per_word];
        if (value) {
            w |= mask;
        } else {
            w &= ~mask;
        }
    }

    void flip(std::size_t i) noexcept {
        words_[i / bits_per_word] ^= (word_type{1} << (i % bits_per_word));
    }

    // Number of set bits. Any padding bits beyond `bits_` are always kept zero
    // (set/flip only touch indices < size), so a plain word popcount is exact.
    std::size_t popcount() const noexcept {
        std::size_t total = 0;
        for (word_type w : words_) {
            total += static_cast<std::size_t>(std::popcount(w));
        }
        return total;
    }

    bool is_zero() const noexcept {
        for (word_type w : words_) {
            if (w != 0) return false;
        }
        return true;
    }

    // In-place GF(2) addition (xor). Requires equal size.
    BitVec& operator^=(const BitVec& other) noexcept {
        const std::size_t n = words_.size();
        for (std::size_t k = 0; k < n; ++k) {
            words_[k] ^= other.words_[k];
        }
        return *this;
    }

    bool operator==(const BitVec& other) const noexcept {
        return bits_ == other.bits_ && words_ == other.words_;
    }

    // Word-level access for matrix kernels.
    word_type word(std::size_t k) const noexcept { return words_[k]; }

private:
    std::size_t            bits_{0};
    std::vector<word_type> words_{};
};

inline BitVec operator^(BitVec lhs, const BitVec& rhs) noexcept {
    lhs ^= rhs;
    return lhs;
}

}  // namespace aleph::linalg::gf2
