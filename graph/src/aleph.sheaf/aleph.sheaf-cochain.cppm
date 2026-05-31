// Cellular cochain complex C^k(F) with GF(2) coefficients.
//
// A CochainLayout packs the stalks of all k-simplices into a flat bit layout:
// for each simplex σ of dimension k (in the FlagComplex's level order) it
// reserves `dim_stalk(σ)` consecutive bits. The coboundary operator
// δ^k : C^k → C^{k+1} over GF(2) is then a BitMatrix whose rows are indexed by
// the C^{k+1} layout and columns by the C^k layout: for every cofacet τ of
// dimension k+1 and every k-face σ of τ, the restriction ρ_{σ→τ} is XORed into
// the (τ-block, σ-block) sub-block. The signed boundary signs collapse to 1
// over GF(2), so the alternating sum is just an XOR, which is what makes
// δ^{k+1} ∘ δ^k vanish.
//
// Ported from aleph-engine/aleph-sheaf/src/cochain.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust `CochainLayout.per_simplex` is an `IndexMap<Simplex, Range<usize>>`
//     (insertion-ordered). C++ uses `OrderedMap<Simplex, BitRange>`, which is
//     also insertion-ordered (head/tail linked list) and keys on the provided
//     `std::hash<Simplex>`. `OrderedMap` is move-only, so `CochainLayout` is
//     move-only too (it is built once per (sheaf, complex, k) and not cloned).
//     The Rust `Range<usize>` becomes a tiny `BitRange{start, end}` value.
//   * Rust passes layouts around as `Arc<CochainLayout>` and stores them in
//     `Cochain`/`CoboundaryMatrix`. With no clone and no shared-ownership need
//     here, `Cochain` holds a non-owning `const CochainLayout*` (the layout must
//     outlive the cochain — a programmer-error contract, guarded by assert).
//   * Rust's panicking index `self.layout.per_simplex[sigma]` and the
//     `.expect("tau_stalk ⊆ sigma_stalk")` become `assert` here, matching the
//     no-exceptions ISA build (Graph::insert_node aborts on precondition
//     violation).
//   * The headline operator is the generic free function
//     `coboundary_matrix<S>(sheaf, complex, k) -> BitMatrix` (templated on the
//     `CellularZ2Sheaf` concept), exactly as in the Rust generic
//     `coboundary_matrix`. It builds the δ^k data directly from
//     `restriction(σ, τ)`. δ^k is a zero-row matrix when k+1 ≥ levels.
//   * Rust `BitMatrix::get/set/rows/cols/mul/is_zero` map to the C++ names
//     `at/set/rows/cols/mul/is_zero`.

module;
#include <cassert>
#include <cstddef>

export module aleph.sheaf:cochain;

import aleph.linalg.gf2;
import aleph.containers;
import :simplex;
import :flag_complex;
import :sheaf_trait;

export namespace aleph::sheaf {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

// A half-open bit range [start, end) inside a flat cochain layout. Mirrors the
// Rust `Range<usize>`; `width()` is `end - start`.
struct BitRange {
    std::size_t start{0};
    std::size_t end{0};

    [[nodiscard]] std::size_t width() const noexcept { return end - start; }
    [[nodiscard]] bool operator==(const BitRange&) const noexcept = default;
};

// Mapping from the k-simplices of a FlagComplex to bit ranges in a flat layout.
// For each σ of dimension k, `dim_stalk(σ)` consecutive bits are reserved, in
// the complex's level order. Move-only (the backing OrderedMap is move-only).
class CochainLayout {
public:
    using RangeMap = aleph::containers::OrderedMap<Simplex, BitRange>;

    CochainLayout() = default;

    // Build the layout for C^k(F): iterate over `complex.simplices[k]` in level
    // order, allocating `dim_stalk(σ)` consecutive bits per σ. If `k` exceeds
    // the complex's dimension (no level k), the layout is empty (0 bits).
    // Ported from `CochainLayout::for_dim`.
    template <CellularZ2Sheaf S>
    [[nodiscard]] static CochainLayout for_dim(const S& sheaf,
                                               const FlagComplex& complex,
                                               std::size_t k) {
        CochainLayout layout;
        layout.k_ = k;
        std::size_t cursor = 0;
        if (k < complex.simplices.size()) {
            for (const Simplex& sigma : complex.simplices[k]) {
                const std::size_t width = sheaf.dim_stalk(sigma);
                layout.per_simplex_.insert(sigma, BitRange{cursor, cursor + width});
                cursor += width;
            }
        }
        layout.total_bits_ = cursor;
        return layout;
    }

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::size_t total_bits() const noexcept { return total_bits_; }

    // Number of simplices in this layout (per_simplex.len() in Rust).
    [[nodiscard]] std::size_t simplex_count() const noexcept {
        return per_simplex_.size();
    }

    // The bit range reserved for σ, or nullptr if σ is not in this layout.
    [[nodiscard]] const BitRange* range_of(const Simplex& sigma) const noexcept {
        return per_simplex_.get(sigma);
    }

    [[nodiscard]] bool contains(const Simplex& sigma) const noexcept {
        return per_simplex_.contains(sigma);
    }

    // Const access to the backing range map (move-only; insertion-ordered).
    [[nodiscard]] const RangeMap& per_simplex() const noexcept { return per_simplex_; }

private:
    std::size_t k_{0};
    RangeMap    per_simplex_{};
    std::size_t total_bits_{0};
};

// A cochain in C^k(F): a flat bit vector over a CochainLayout. The layout is
// borrowed (non-owning) and must outlive the cochain. Ported from `Cochain`.
class Cochain {
public:
    // All-zero cochain over `layout`. `layout` must outlive this Cochain.
    [[nodiscard]] static Cochain zeros(const CochainLayout& layout) {
        Cochain c;
        c.layout_ = &layout;
        c.bits_   = BitVec(layout.total_bits());
        return c;
    }

    // Read bit `local_idx` of stalk(σ). Precondition: σ is in the layout and
    // `local_idx` is within its width (programmer error otherwise).
    [[nodiscard]] bool get(const Simplex& sigma, std::size_t local_idx) const {
        const BitRange* range = layout_->range_of(sigma);
        assert(range != nullptr && "Cochain::get: simplex not in layout");
        assert(local_idx < range->width() && "Cochain::get: local index out of range");
        return bits_.get(range->start + local_idx);
    }

    // Write bit `local_idx` of stalk(σ). Same precondition as `get`.
    void set(const Simplex& sigma, std::size_t local_idx, bool v) {
        const BitRange* range = layout_->range_of(sigma);
        assert(range != nullptr && "Cochain::set: simplex not in layout");
        assert(local_idx < range->width() && "Cochain::set: local index out of range");
        bits_.set(range->start + local_idx, v);
    }

    [[nodiscard]] const CochainLayout& layout() const noexcept { return *layout_; }
    [[nodiscard]] const BitVec& bits() const noexcept { return bits_; }

private:
    const CochainLayout* layout_{nullptr};
    BitVec               bits_{};
};

// Build the coboundary operator δ^k : C^k(F) → C^{k+1}(F) over GF(2) as a
// BitMatrix. Rows are indexed by the C^{k+1} layout, columns by the C^k layout.
// If `k + 1 ≥ levels`, the target layout is empty and δ^k has zero rows.
//
// For every cofacet τ of dimension k+1 and every k-face σ of τ, the restriction
// ρ_{σ→τ} = restriction(σ, τ) (a dim_stalk(τ) × dim_stalk(σ) matrix) is XORed
// into the (τ-row-block, σ-col-block) sub-block of δ^k. Over GF(2) the
// boundary signs vanish, so the cofacet contributions add by XOR — which is
// exactly what gives δ^{k+1} ∘ δ^k = 0.
//
// Ported from the generic free function `coboundary_matrix<S: CellularZ2Sheaf>`.
template <CellularZ2Sheaf S>
[[nodiscard]] BitMatrix coboundary_matrix(const S& sheaf,
                                          const FlagComplex& complex,
                                          std::size_t k) {
    const CochainLayout source = CochainLayout::for_dim(sheaf, complex, k);
    const CochainLayout target = CochainLayout::for_dim(sheaf, complex, k + 1);
    BitMatrix data(target.total_bits(), source.total_bits());
    if (k + 1 >= complex.simplices.size()) {
        return data;
    }
    for (const Simplex& tau : complex.simplices[k + 1]) {
        const BitRange* tau_range = target.range_of(tau);
        assert(tau_range != nullptr && "coboundary: cofacet missing from target layout");
        for (const Simplex& sigma : faces_of_dim(tau, k)) {
            const BitRange* sigma_range = source.range_of(sigma);
            assert(sigma_range != nullptr && "coboundary: face missing from source layout");
            const BitMatrix r = sheaf.restriction(sigma, tau);
            for (std::size_t i = 0; i < r.rows(); ++i) {
                for (std::size_t j = 0; j < r.cols(); ++j) {
                    if (r.at(i, j)) {
                        const std::size_t row = tau_range->start + i;
                        const std::size_t col = sigma_range->start + j;
                        data.set(row, col, !data.at(row, col));
                    }
                }
            }
        }
    }
    return data;
}

}  // namespace aleph::sheaf
