// The connecting morphism ∂: H⁰(F|_K) → H¹(F_{G'}) from the Mayer-Vietoris
// long exact sequence.
//
// Concretely:
//   1. Given a section class [s_K] ∈ H⁰(K), lift to a (non-section) cochain
//      s_G ∈ C⁰(G') by extending with 0 on non-K vertices.
//   2. Compute c := δ⁰_{G'}(s_G) ∈ C¹(G'). Because s_K was a section over K, c
//      vanishes on K-internal edges and lives on the "boundary" edges where U
//      meets R.
//   3. c ∈ ker δ¹_{G'} (by δ²=0), so project to H¹(G') modulo im δ⁰_{G'}. The
//      residue is ∂([s_K]).
//
// Ported from aleph-engine/aleph-sheaf/src/connecting.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * The Rust file imports `CochainLayout`/`coboundary_matrix` from `cochain.rs`
//     and `compute_hk` from `cohomology.rs`. Here the cochain machinery is the
//     sibling partition :cochain (imported below) and used directly — its
//     `CochainLayout` (move-only, `for_dim`/`range_of`/`total_bits`/
//     `per_simplex`) and generic `coboundary_matrix<S>` are the faithful ports.
//     The slice of cohomology this morphism consumes — the H⁰ kernel basis (the
//     columns of ∂) and the H¹ target dimension (the empty-domain fallback) —
//     is the rank-nullity computation from cohomology.rs::compute_hk, ported as
//     a small `connecting_detail::h_kernel_basis` / `h_dim` pair so :connecting
//     does not have to wait on the :cohomology partition. Their formula mirrors
//     compute_hk line-for-line (δ^{-1} := 0, saturating subtraction).
//   * BitMatrix API drift (Rust → C++ gf2):
//       - Rust `d0.data.apply(&s_g)`   → C++ `BitMatrix::apply(const BitVec&)`.
//       - Rust `d0.data.image_basis()` → C++ `BitMatrix::image_basis()` (the
//         independent original columns as rows-wide BitVecs).
//       - Rust `BitMatrix::from_cols(&cols)` infers the row count from the
//         columns; the C++ `from_cols(std::span<const BitVec>, std::size_t
//         nrows)` takes an explicit row count. That argument is load-bearing in
//         the two degenerate paths Rust handled implicitly: zero columns and
//         all-zero columns (we still report dim C¹(G') rows). We pass
//         `d0.rows()` = dim C¹(G').
//       - Rust `project_to_h1` mutates `c` via `d0_image_basis.reduce_modulo_
//         image(c)` (the receiver IS the packed image matrix). The C++
//         `reduce_modulo_image(const BitVec& v, std::span<const BitVec> image)`
//         is non-mutating and takes the image basis directly as the span, so we
//         neither pack the basis into a BitMatrix nor mutate in place: the C++
//         `project_to_h1` keeps the in-out `BitVec&` shape by assigning the
//         residue back.
//   * Sheaf surface uses the C++ concept method names: `dim_stalk` (Rust
//     `stalk_dim`), `restriction`, `lift_basis_index`.
//   * No exceptions (aleph_flags_isa): Rust `assert_eq!`/index panics become
//     `assert`; `lift_basis_index` already returns std::optional.

module;
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.sheaf:connecting;

import aleph.linalg.gf2;
import :simplex;
import :flag_complex;
import :sheaf_trait;
import :cochain;

namespace aleph::sheaf::connecting_detail {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

// The H^k cohomology dimension via rank-nullity, ported from
// cohomology.rs::compute_hk: dim H^k = (dim C^k − rank δ^k) − rank δ^{k-1}, with
// δ^{-1} := 0 and saturating subtraction. Used only for the H¹(G') target dim
// in the empty-domain fallback.
template <CellularZ2Sheaf S>
[[nodiscard]] std::size_t h_dim(const S& sheaf, const FlagComplex& complex, std::size_t k) {
    const CochainLayout layout = CochainLayout::for_dim(sheaf, complex, k);
    const std::size_t cochain_dim = layout.total_bits();
    const std::size_t rank_curr = coboundary_matrix(sheaf, complex, k).rank();
    const std::size_t rank_prev =
        (k == 0) ? std::size_t{0} : coboundary_matrix(sheaf, complex, k - 1).rank();
    const std::size_t kernel_dim = (cochain_dim > rank_curr) ? cochain_dim - rank_curr : 0;
    return (kernel_dim > rank_prev) ? kernel_dim - rank_prev : 0;
}

// A basis of ker δ^k as C^k-wide vectors, ported from compute_hk's
// `kernel_basis = d_curr.data.kernel_basis()`. For k = 0 (δ^{-1} := 0) this is
// exactly the basis of H⁰, i.e. the columns of ∂.
template <CellularZ2Sheaf S>
[[nodiscard]] std::vector<BitVec> h_kernel_basis(const S& sheaf, const FlagComplex& complex,
                                                 std::size_t k) {
    return coboundary_matrix(sheaf, complex, k).kernel_basis();
}

}  // namespace aleph::sheaf::connecting_detail

export namespace aleph::sheaf {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

// Lift a C⁰(K) element (a BitVec in the K-layout) to a C⁰(G') element. For each
// K-vertex σ present in both layouts and each set basis bit i_k of F_K(σ), use
// `k_sheaf.lift_basis_index(σ, i_k, g_sheaf)` to find the corresponding basis
// index i_g in F_{G'}(σ) and set that bit in the G' output. K-vertices absent
// from the G' layout are skipped. Ported from `lift_k_to_g_prime`.
template <CellularZ2Sheaf S>
[[nodiscard]] BitVec lift_k_to_g_prime(const BitVec& k_vec, const CochainLayout& k_layout,
                                       const S& k_sheaf, const CochainLayout& g_layout,
                                       const S& g_sheaf) {
    assert(k_vec.size() == k_layout.total_bits() && "k_vec length mismatch");
    BitVec out(g_layout.total_bits());
    for (const auto& [sigma, k_range] : k_layout.per_simplex()) {
        // Skip simplices that don't appear in the G' layout.
        const aleph::sheaf::BitRange* g_range = g_layout.range_of(sigma);
        if (g_range == nullptr) continue;
        const std::size_t k_dim = k_sheaf.dim_stalk(sigma);
        for (std::size_t i_k = 0; i_k < k_dim; ++i_k) {
            if (!k_vec.get(k_range.start + i_k)) continue;
            const std::optional<std::size_t> i_g = k_sheaf.lift_basis_index(sigma, i_k, g_sheaf);
            if (i_g.has_value()) {
                out.set(g_range->start + *i_g, true);
            }
        }
    }
    return out;
}

// Project a C¹(G') element (already in ker δ¹_{G'}) to its class in H¹(G'),
// modulo the image basis of δ⁰_{G'}. The residue replaces `c`. Ported from
// `project_to_h1` — the Rust mutates `c` in place; the C++ gf2 reduce is
// non-mutating, so we assign the residue back to preserve the in-out shape.
inline void project_to_h1(BitVec& c, std::span<const BitVec> d0_image_basis) {
    // Any BitMatrix instance can host reduce_modulo_image; it only reads the
    // `image` span and the vector `c`. Use an empty matrix as the receiver.
    const BitMatrix host{};
    c = host.reduce_modulo_image(c, d0_image_basis);
}

// Construct the connecting morphism ∂: H⁰(F|_K) → H¹(F_{G'}) as a BitMatrix.
// Columns are indexed by a basis of H⁰(K) (the kernel basis of δ⁰_K); rows are
// the dim C¹(G') coordinates the residue lives in. Ported from
// `connecting_morphism`.
template <CellularZ2Sheaf S>
[[nodiscard]] BitMatrix connecting_morphism(const S& g_sheaf, const FlagComplex& g_complex,
                                            const S& k_sheaf, const FlagComplex& k_complex) {
    const std::vector<BitVec> h0_k_basis =
        connecting_detail::h_kernel_basis(k_sheaf, k_complex, 0);
    if (h0_k_basis.empty()) {
        // No H⁰(K) classes to map → ∂ is the zero map from 0 dims. The Rust
        // returns `BitMatrix::zeros(h1_gp.dim, 0)`, i.e. the row count is the H¹
        // *cohomology* dimension of G' (NOT dim C¹). We port the code, not the
        // comment beside it: the row count is `h_dim(g_sheaf, g_complex, 1)`.
        // (rank() is 0 either way, so the oracle is unaffected; this keeps the
        // shape identical to the reference.)
        const std::size_t h1_gp_dim = connecting_detail::h_dim(g_sheaf, g_complex, 1);
        return BitMatrix(h1_gp_dim, 0);
    }

    const CochainLayout k_layout = CochainLayout::for_dim(k_sheaf, k_complex, 0);
    const CochainLayout g_layout_0 = CochainLayout::for_dim(g_sheaf, g_complex, 0);
    const BitMatrix d0 = coboundary_matrix(g_sheaf, g_complex, 0);
    const std::vector<BitVec> d0_image_cols = d0.image_basis();

    const std::size_t target_rows = d0.rows();  // dim C¹(G')
    std::vector<BitVec> cols;
    cols.reserve(h0_k_basis.size());
    for (const BitVec& k_class : h0_k_basis) {
        BitVec s_g = lift_k_to_g_prime(k_class, k_layout, k_sheaf, g_layout_0, g_sheaf);
        BitVec c = d0.apply(s_g);
        project_to_h1(c, std::span<const BitVec>(d0_image_cols));
        cols.push_back(std::move(c));
    }
    return BitMatrix::from_cols(std::span<const BitVec>(cols), target_rows);
}

}  // namespace aleph::sheaf
