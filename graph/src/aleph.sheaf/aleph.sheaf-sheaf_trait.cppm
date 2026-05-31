// Concept abstracting the access pattern used by the cochain machinery.
//
// A cellular sheaf over GF(2) has finite-dimensional Z/2-vector spaces as
// stalks at each simplex, plus Z/2-linear restriction maps for face relations.
// Implementations MUST satisfy the cochain composability law: for ρ ⊂ σ ⊂ τ,
//
//     restriction(σ, τ) * restriction(ρ, σ) == restriction(ρ, τ)
//
// (matrix product over GF(2)). This is what gives δ² = 0.
//
// Ported from aleph-engine/aleph-sheaf/src/sheaf_trait.rs.
//
// Adaptation notes (vs. the Rust `trait CellularZ2Sheaf`):
//   * Rust models this as a trait with three methods; C++26 expresses the same
//     access contract as a `concept` so the cochain/cohomology code can be
//     templated on the concrete sheaf type (static dispatch, no vtable). The
//     three required expressions mirror the three trait methods exactly.
//   * Method names follow the C++ side of the 4c plan: `dim_stalk` (Rust
//     `stalk_dim`) and `restriction`. These names are used consistently across
//     :sheaf_trait, the concrete sheaves, and cluster C.
//   * Rust `restriction` returns a `stalk_dim(τ) × stalk_dim(σ)` BitMatrix
//     (rows = dim_stalk(tau), cols = dim_stalk(sigma)); the concept only pins
//     the return *type*; the dimension contract is documented and exercised by
//     the concrete-sheaf tests.
//   * Rust `lift_basis_index(&self, σ, idx, &Self) -> Option<usize>` becomes a
//     requirement returning `std::optional<std::size_t>`. The `other` argument
//     is the same sheaf type `S` (a "supersheaf", same kind, possibly larger
//     stalks); :connecting consumes this to lift K-sections into G'. `None` /
//     `std::nullopt` means the basis element has no image in the supersheaf.
//   * The C++ BitMatrix lives in aleph.linalg.gf2 and uses C++ method names
//     (rows/cols/at/...); we only name the type here.

module;
#include <concepts>
#include <cstddef>
#include <optional>

export module aleph.sheaf:sheaf_trait;

import aleph.linalg.gf2;
import :simplex;

export namespace aleph::sheaf {

using aleph::linalg::gf2::BitMatrix;

// CellularZ2Sheaf: a type modelling a cellular sheaf over GF(2) on the flag
// complex. A conforming `S` provides:
//
//   std::size_t dim_stalk(const Simplex& sigma) const;
//       dim F(σ) as a Z/2 vector space.
//
//   BitMatrix restriction(const Simplex& sigma, const Simplex& tau) const;
//       The Z/2-linear map ρ_{σ→τ}: F(σ) → F(τ) where σ is a face of τ,
//       returned as a dim_stalk(τ) × dim_stalk(σ) matrix (rows × cols).
//
//   std::optional<std::size_t>
//   lift_basis_index(const Simplex& sigma, std::size_t idx, const S& other) const;
//       Lift basis element `idx` of this sheaf's stalk at σ into a basis
//       element index of `other`'s stalk at σ, where `other` is a supersheaf
//       (same kind, possibly larger stalks). `std::nullopt` if the basis
//       element has no image in the supersheaf at σ. Used by :connecting to
//       lift K-sections to G'.
//
// Implementations MUST satisfy the composability law documented above so that
// the coboundary squares to zero.
template <typename S>
concept CellularZ2Sheaf =
    requires(const S& sheaf, const Simplex& sigma, const Simplex& tau, std::size_t idx) {
        { sheaf.dim_stalk(sigma) } -> std::same_as<std::size_t>;
        { sheaf.restriction(sigma, tau) } -> std::same_as<BitMatrix>;
        { sheaf.lift_basis_index(sigma, idx, sheaf) } -> std::same_as<std::optional<std::size_t>>;
    };

}  // namespace aleph::sheaf
