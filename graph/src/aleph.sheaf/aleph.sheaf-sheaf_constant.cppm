// The constant Z/2 sheaf on a flag complex.
//
// F(σ) = Z/2 for every simplex σ, with restriction maps = identity 1×1
// matrices. Its cohomology IS the cellular cohomology of the complex with
// Z/2 coefficients: H⁰ = #components, H¹ = #independent 1-cycles, H^k = 0 for
// k > 1 on graphs / 2-complexes.
//
// Demonstrator for a non-trivial connecting morphism: a 4-mesh cycle has
// H¹ = 1; the TRIANGULATE rule reduces it to 0 by adding a diagonal, and the
// MV certificate then sees ε_sheaf > 0.
//
// Ported from aleph-engine/aleph-sheaf/src/constant_sheaf.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Trait method names follow the C++ sheaf convention (`dim_stalk` rather
//     than Rust's `stalk_dim`); `restriction` and `lift_basis_index` keep their
//     names. The C++26 `CellularZ2Sheaf` concept ships in :sheaf_trait
//     (a sibling partition); this partition only needs the concrete sheaf, so
//     it depends on :simplex/:flag_complex and aleph.linalg.gf2 directly and
//     does not import the (still-evolving) trait partition. The method
//     signatures are kept exactly concept-compatible.
//   * The Rust struct borrows the complex (`complex: &'a FlagComplex`). C++
//     mirrors that with a non-owning `const FlagComplex*`; the complex must
//     outlive the sheaf. The pointer is a context hook for future variants
//     (constant-with-twist, etc.) and is currently unused at the trait-method
//     level, matching the Rust comment.
//   * BitMatrix API drift: the C++ aleph.linalg.gf2::BitMatrix exposes
//     `identity(n)`, `rows()`, `cols()`, `at(r,c)` (the Rust reference used
//     fields `rows`/`cols` and `get`). We use the C++ names.
//   * No exceptions (aleph_flags_isa): `lift_basis_index` returns
//     std::optional, matching the Rust `Option<usize>`.

module;
#include <cstddef>
#include <optional>

export module aleph.sheaf:sheaf_constant;

import aleph.linalg.gf2;
import :simplex;
import :flag_complex;

export namespace aleph::sheaf {

// ConstantZ2Sheaf: the constant sheaf with stalk Z/2 at every simplex and
// identity restriction maps. Models the CellularZ2Sheaf concept.
class ConstantZ2Sheaf {
public:
    // Construct from a flag complex the sheaf lives over. The complex is
    // borrowed (non-owning) and must outlive the sheaf. Mirrors the Rust
    // `ConstantZ2Sheaf::new(complex: &FlagComplex)`.
    explicit ConstantZ2Sheaf(const FlagComplex& complex) noexcept
        : complex_(&complex) {}

    // The flag complex this sheaf is defined over (context hook; currently
    // unused at the trait-method level, kept for future twisted variants).
    [[nodiscard]] const FlagComplex& complex() const noexcept { return *complex_; }

    // dim F(σ) as a Z/2 vector space: always 1 for the constant sheaf.
    [[nodiscard]] std::size_t dim_stalk(const Simplex& sigma) const noexcept {
        (void)sigma;
        return 1;
    }

    // The Z/2-linear restriction map ρ_{σ→τ}: F(σ) → F(τ). For the constant
    // sheaf every stalk is 1-dimensional and the map is the identity, so this
    // is the 1×1 identity matrix regardless of σ, τ.
    [[nodiscard]] aleph::linalg::gf2::BitMatrix
    restriction(const Simplex& sigma, const Simplex& tau) const {
        (void)sigma;
        (void)tau;
        return aleph::linalg::gf2::BitMatrix::identity(1);
    }

    // Lift basis element `idx` of this sheaf's stalk at σ into a basis-element
    // index of `other`'s stalk at σ, where `other` is a supersheaf of the same
    // kind. Both sides are 1-dimensional and basis-aligned, so idx 0 maps to 0
    // and anything else has no image. Mirrors the Rust
    //   if idx == 0 { Some(0) } else { None }.
    [[nodiscard]] std::optional<std::size_t>
    lift_basis_index(const Simplex& sigma, std::size_t idx,
                     const ConstantZ2Sheaf& other) const noexcept {
        (void)sigma;
        (void)other;
        if (idx == 0) return std::optional<std::size_t>{0};
        return std::nullopt;
    }

private:
    const FlagComplex* complex_;
};

}  // namespace aleph::sheaf
