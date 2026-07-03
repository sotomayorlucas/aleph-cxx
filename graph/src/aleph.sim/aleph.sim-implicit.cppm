module;
// Implicit (unconditionally stable) stepping on the shared Δ (spec 2026-07-03).
//
// The explicit steppers (:wave, :diffuse) are matvec-only and CFL-bounded; a
// stiff Δ or a large dt diverges. The implicit steppers solve
//
//     (I + β·Δ) u⁺ = rhs
//
// instead. Δ is PSD, so I + β·Δ is SPD for every β ≥ 0: the LDLᵀ always exists
// and its solve never touches the kernel guard. One factorization per (Δ, β)
// is reused across MANY steps (steps ≫ edits); on a topology edit the caller
// re-makes the stepper, exactly as the operator itself is rebuilt.
//
// C++-native slice (no Rust oracle — like :vector_field). Determinism: pure
// f64, fixed elimination/solve order, no RNG.
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.sim:implicit;

import aleph.math;            // f64
import aleph.linalg.sparse;   // DMatrix, LDLT
import :section;              // Section, StepError
import :wave;                 // WaveParams
import :diffuse;              // DiffuseParams

export namespace aleph::sim {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

// Value-less tags (HelmholtzError precedent): make() discards the LdltErrorInfo.
enum class ImplicitError {
    InvalidShift,   // beta (resp. dt) not finite or not positive where required
    FactorFailed,   // LDLT rejected I + beta*delta (not square / not symmetric)
};

// The factored (I + β·Δ) carrier both implicit steppers solve against.
struct ShiftedLaplacian {
    std::size_t                 n{0};
    f64                         beta{0.0};
    aleph::linalg::sparse::LDLT ldlt;

    // H = I + beta*delta, LDLT-factored. beta must be finite and >= 0.
    [[nodiscard]] static std::expected<ShiftedLaplacian, ImplicitError>
    make(const DMatrix& delta, f64 beta) {
        if (!std::isfinite(beta) || beta < 0.0) {
            return std::unexpected(ImplicitError::InvalidShift);
        }
        const std::size_t n = delta.rows();
        DMatrix h = DMatrix::zeros(n, delta.cols());
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < delta.cols(); ++j) {
                h.at(i, j) = beta * delta.at(i, j);
            }
        }
        for (std::size_t i = 0; i < n && i < delta.cols(); ++i) {
            h.at(i, i) += 1.0;
        }
        auto f = aleph::linalg::sparse::LDLT::factorize(h);
        if (!f.has_value()) {
            return std::unexpected(ImplicitError::FactorFailed);
        }
        return ShiftedLaplacian{n, beta, std::move(*f)};
    }

    // Solve (I + β·Δ) x = b. SPD ⇒ the kernel guard is unreachable; a nullopt
    // can only arise from a size mismatch (defensive).
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(std::span<const f64> b) const {
        return ldlt.solve(b);
    }
};

// Backward-Euler heat on the shared Δ:  (I + dt·α·Δ) u⁺ = u.
// Unconditionally stable (the eigenvalues of (I+βΔ)⁻¹ lie in (0, 1]); mass
// Σu is conserved exactly in exact arithmetic (1ᵀΔ = 0); variance decreases
// monotonically.
//
// CONTRACT (stronger than the explicit steppers): the solve produces a full
// candidate before any write-back, so on ANY error the section is UNCHANGED.
struct ImplicitDiffuseStepper {
    DiffuseParams    params{};
    ShiftedLaplacian op{};   // factor of (I + dt·α·Δ)
    f64              dt{0.0};

    [[nodiscard]] static std::expected<ImplicitDiffuseStepper, ImplicitError>
    make(const DMatrix& delta, DiffuseParams p, f64 dt) {
        if (!std::isfinite(dt) || dt <= 0.0) {
            return std::unexpected(ImplicitError::InvalidShift);
        }
        auto op = ShiftedLaplacian::make(delta, dt * p.alpha);
        if (!op.has_value()) {
            return std::unexpected(op.error());
        }
        return ImplicitDiffuseStepper{p, std::move(*op), dt};
    }

    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (op.n != n) return std::unexpected(StepError::DimMismatch);
        const auto x = op.solve(std::span<const f64>(u.data.data(), n));
        if (!x.has_value()) return std::unexpected(StepError::NonFinite);
        for (const f64 xi : *x) {
            if (!std::isfinite(xi)) return std::unexpected(StepError::NonFinite);
        }
        u.data = *x;
        return {};
    }
};

// Backward-Euler wave on the shared Δ, damp-then-force convention consistent
// with the explicit WaveStepper:
//     v⁺ = damping·v − dt·c²·Δu⁺,   u⁺ = u + dt·v⁺
//  ⇒  (I + dt²c²·Δ) u⁺ = u + dt·damping·v,   then   v⁺ = (u⁺ − u)/dt.
// A-stable; adds numerical damping on top of the model's own `damping`
// (acceptable: backward Euler only removes energy — no blow-up at any dt).
// Same untouched-on-error contract as ImplicitDiffuseStepper.
struct ImplicitWaveStepper {
    WaveParams       params{};
    ShiftedLaplacian op{};   // factor of (I + dt²·c²·Δ)
    f64              dt{0.0};

    [[nodiscard]] static std::expected<ImplicitWaveStepper, ImplicitError>
    make(const DMatrix& delta, WaveParams p, f64 dt) {
        if (!std::isfinite(dt) || dt <= 0.0) {
            return std::unexpected(ImplicitError::InvalidShift);
        }
        auto op = ShiftedLaplacian::make(delta, dt * dt * p.c * p.c);
        if (!op.has_value()) {
            return std::unexpected(op.error());
        }
        return ImplicitWaveStepper{p, std::move(*op), dt};
    }

    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u, Section<f64>& v) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (op.n != n || v.size() != n)
            return std::unexpected(StepError::DimMismatch);

        std::vector<f64> rhs(n);
        for (std::size_t i = 0; i < n; ++i) {
            rhs[i] = u.data[i] + dt * params.damping * v.data[i];
        }
        const auto x = op.solve(std::span<const f64>(rhs.data(), n));
        if (!x.has_value()) return std::unexpected(StepError::NonFinite);
        for (const f64 xi : *x) {
            if (!std::isfinite(xi)) return std::unexpected(StepError::NonFinite);
        }
        for (std::size_t i = 0; i < n; ++i) {
            v.data[i] = ((*x)[i] - u.data[i]) / dt;
        }
        u.data = *x;
        return {};
    }
};

}  // namespace aleph::sim
