// M28 — Discrete Exterior Calculus (DEC) framework.
//
// A minimal abstraction layer over cellular cochain complexes: a `Coeffs`
// coefficient ring, a `Form<C>` k-cochain, and the cellular coboundary
// operator `d : C^k -> C^{k+1}` derived from simplicial face inclusion. The
// foundational certificate is `d(d(form)) == 0` for every `Coeffs` impl.
//
// Ported from aleph-engine/aleph-sheaf/src/dec.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust models the coefficient ring as a `Coeffs` trait with methods
//     `zero()/add/neg/add_signed`. C++26 expresses this as a `Coeffs` concept
//     (spec section 6.3) requiring `C::zero()`, `a.add(b)`, `a.neg()`, `==`.
//     The primitive coefficient spaces (Z/2, R, R^3) cannot carry member
//     functions, so each ships as a thin value-type wrapper:
//        Z2  wraps bool   — add = XOR, neg = identity (-1 == 1 mod 2)
//        R64 wraps double  — add = +, neg = unary minus  (Rust used f32; the
//                            C++ provided impl is f64 per the spec)
//        R3  wraps aleph::math::Vec3 (Rust used [f32;3]; spec calls for Vec3)
//     All three are trivially copyable, matching the Rust `Coeffs: Copy`.
//   * Rust's `Coeffs::add_signed(other, positive)` default
//     (`positive ? add(other) : add(other.neg())`) is ported as the free
//     template `add_signed<C>(a, b, positive)`, since the concept itself only
//     mandates zero/add/neg/==.
//   * Rust's `Form { degree, values }` becomes `Form { k, coeffs }` (spec
//     field names). The member `coboundary(&complex)` becomes the free
//     function `d(complex, form)` (spec signature).
//   * `coboundary`'s face enumeration is ported verbatim: each boundary face
//     of a (k+1)-cell tau is tau with one vertex deleted; tau is already
//     sorted ascending (canonical Simplex), so deleting a vertex preserves the
//     canonical order and the resulting `sigma` compares directly against the
//     source cells (no re-canonicalisation, matching the Rust). The face sign
//     is +1 for even drop position, -1 for odd. Linear search over source
//     cells mirrors the Rust `position(..)`.

module;
#include <concepts>
#include <cstddef>
#include <vector>

export module aleph.sheaf:dec;

import aleph.math;
import :simplex;
import :flag_complex;

export namespace aleph::sheaf {

// ─────────────────────────────────────────────────────────────────────────────
// Coeffs concept (spec 6.3): the coefficient ring fixing the algebra `d` needs
// — additive identity, addition, and sign-flip (orientation-aware faces).
// ─────────────────────────────────────────────────────────────────────────────
template <typename C>
concept Coeffs = requires(C a, C b) {
    { C::zero() } -> std::same_as<C>;
    { a.add(b) } -> std::same_as<C>;
    { a.neg() } -> std::same_as<C>;
    { a == b } -> std::convertible_to<bool>;
};

// `a + sign * b` — the Rust `Coeffs::add_signed` default, lifted to a free
// helper. `positive` selects + vs. the negated summand.
template <typename C>
[[nodiscard]] constexpr C add_signed(C a, C b, bool positive) {
    return positive ? a.add(b) : a.add(b.neg());
}

// ── Provided coefficient impls ───────────────────────────────────────────────

// Z/2 (GF(2)). Value-type wrapper over bool. add = XOR; neg is identity
// because -1 == 1 in Z/2.
struct Z2 {
    bool value{false};

    [[nodiscard]] static constexpr Z2 zero() noexcept { return Z2{false}; }
    [[nodiscard]] constexpr Z2 add(Z2 other) const noexcept {
        return Z2{static_cast<bool>(value ^ other.value)};
    }
    [[nodiscard]] constexpr Z2 neg() const noexcept { return *this; }
    [[nodiscard]] constexpr bool operator==(Z2 other) const noexcept {
        return value == other.value;
    }
};

// R — the field of reals, as f64. (Rust's DEC used f32; the C++ provided impl
// is f64 per spec 6.3.)
struct R64 {
    double value{0.0};

    [[nodiscard]] static constexpr R64 zero() noexcept { return R64{0.0}; }
    [[nodiscard]] constexpr R64 add(R64 other) const noexcept {
        return R64{value + other.value};
    }
    [[nodiscard]] constexpr R64 neg() const noexcept { return R64{-value}; }
    [[nodiscard]] constexpr bool operator==(R64 other) const noexcept {
        return value == other.value;
    }
};

// R^3 — the SPD coefficient space, as aleph::math::Vec3. (Rust used [f32;3].)
struct R3 {
    aleph::math::Vec3 value{};

    [[nodiscard]] static constexpr R3 zero() noexcept {
        return R3{aleph::math::Vec3{0.0f, 0.0f, 0.0f}};
    }
    [[nodiscard]] constexpr R3 add(R3 other) const noexcept {
        return R3{value + other.value};
    }
    [[nodiscard]] constexpr R3 neg() const noexcept { return R3{-value}; }
    [[nodiscard]] constexpr bool operator==(R3 other) const noexcept {
        return value == other.value;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Form<C>: a k-cochain over a flag complex.
//
// Storage: one `C` per k-cell, indexed by the flag complex's `simplices[k]`
// order. Construction does NOT validate the cell count against the complex —
// callers must ensure `coeffs.size() == complex.simplices[k].size()`
// (matching the Rust note).
// ─────────────────────────────────────────────────────────────────────────────
template <Coeffs C>
struct Form {
    std::size_t    k{0};       // cochain degree
    std::vector<C> coeffs{};   // one coefficient per k-cell, simplices[k] order

    [[nodiscard]] static Form<C> zero(std::size_t degree, std::size_t n_cells) {
        return Form<C>{degree, std::vector<C>(n_cells, C::zero())};
    }
};

// Apply the cellular coboundary operator `d : C^k -> C^{k+1}`.
//
// For a (k+1)-cell tau with boundary k-cells sigma_1, .., sigma_{k+2}:
//   (d c)(tau) = Σ_i sign(sigma_i : tau) · c(sigma_i)
// where sign is +1 if orientations agree, -1 if not. For the flag complexes
// here (canonical ordered-vertex orientation), the sign is the parity of the
// dropped vertex position: +1 for an even drop index, -1 for an odd one.
template <Coeffs C>
[[nodiscard]] Form<C> d(const FlagComplex& complex, const Form<C>& form) {
    const std::size_t k        = form.k;
    const std::size_t target_k = k + 1;
    if (target_k >= complex.simplices.size()) {
        // No (k+1)-cells exist — the codomain is the zero cochain.
        return Form<C>::zero(target_k, 0);
    }
    const std::vector<Simplex>& target_cells = complex.simplices[target_k];
    const std::vector<Simplex>& source_cells = complex.simplices[k];

    Form<C> out = Form<C>::zero(target_k, target_cells.size());

    for (std::size_t ti = 0; ti < target_cells.size(); ++ti) {
        const Simplex& tau = target_cells[ti];
        // Each boundary face is tau with one vertex deleted. The sign of that
        // face is (-1)^position. tau is canonical (sorted ascending), so the
        // face stays canonical and compares directly against source cells.
        for (std::size_t drop_idx = 0; drop_idx < tau.size(); ++drop_idx) {
            Simplex sigma;
            sigma.reserve(tau.size() - 1);
            for (std::size_t j = 0; j < tau.size(); ++j) {
                if (j != drop_idx) sigma.push_back(tau[j]);
            }
            // Linear search for sigma in source_cells (fine for the small
            // complexes used here), mirroring the Rust `position`.
            for (std::size_t si = 0; si < source_cells.size(); ++si) {
                if (source_cells[si] == sigma) {
                    const bool sign_positive = (drop_idx % 2 == 0);
                    out.coeffs[ti] =
                        add_signed(out.coeffs[ti], form.coeffs[si], sign_positive);
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace aleph::sheaf
