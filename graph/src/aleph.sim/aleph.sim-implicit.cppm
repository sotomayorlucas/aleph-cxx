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
// Dense operator -> dense kernel-aware LDLT; CSR operator -> SparseLdlt
// (Davis elimination tree, deterministic natural ordering). Carrier follows
// the HelmholtzFactor optional-pair pattern, NOT std::variant — a
// module-exported std::variant member in this partition trips GCC-16's
// "Bad file data" cluster bug in every importer (the same toolchain
// fragility the helmholtz partition dodged). Exactly one optional is
// engaged after make(). FP contract across the two factor paths mirrors the
// matvec finding (spec 2026-07-04): solutions agree within solver roundoff
// on these well-conditioned SPD systems (eigenvalues in [1, 1+β·λ_max]),
// NOT bitwise; each path is individually byte-deterministic.
struct ShiftedLaplacian {
    std::size_t n{0};
    f64         beta{0.0};
    std::optional<aleph::linalg::sparse::LDLT>       ldlt{};
    std::optional<aleph::linalg::sparse::SparseLdlt> sldlt{};

    // H = I + beta*delta, dense-LDLT-factored. beta must be finite and >= 0.
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
        ShiftedLaplacian out;
        out.n    = n;
        out.beta = beta;
        out.ldlt.emplace(std::move(*f));
        return out;
    }

    // H = I + beta*delta built directly in CSR (O(nnz); the diagonal entry is
    // updated in place when the row stores one — assemble_sparse always does —
    // and inserted at its sorted position otherwise), SparseLdlt-factored.
    [[nodiscard]] static std::expected<ShiftedLaplacian, ImplicitError>
    make(const aleph::linalg::sparse::CsrMatrix& delta, f64 beta) {
        if (!std::isfinite(beta) || beta < 0.0) {
            return std::unexpected(ImplicitError::InvalidShift);
        }
        const std::size_t n = delta.rows();
        if (delta.cols() != n) {
            return std::unexpected(ImplicitError::FactorFailed);
        }
        const auto& rp = delta.row_ptr();
        const auto& ci = delta.col_idx();
        const auto& vs = delta.values();
        std::vector<std::size_t> hrp(n + 1, 0);
        std::vector<std::size_t> hci;
        std::vector<f64>         hvs;
        hci.reserve(vs.size() + n);
        hvs.reserve(vs.size() + n);
        for (std::size_t i = 0; i < n; ++i) {
            bool placed = false;
            for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
                const std::size_t c = ci[k];
                const f64         v = beta * vs[k];
                if (c == i) {
                    hci.push_back(c);
                    hvs.push_back(v + 1.0);
                    placed = true;
                } else {
                    if (!placed && c > i) {
                        hci.push_back(i);
                        hvs.push_back(1.0);
                        placed = true;
                    }
                    hci.push_back(c);
                    hvs.push_back(v);
                }
            }
            if (!placed) {
                hci.push_back(i);
                hvs.push_back(1.0);
            }
            hrp[i + 1] = hvs.size();
        }
        auto f = aleph::linalg::sparse::SparseLdlt::factorize(
            aleph::linalg::sparse::CsrMatrix::from_parts(
                n, n, std::move(hrp), std::move(hci), std::move(hvs)));
        if (!f.has_value()) {
            return std::unexpected(ImplicitError::FactorFailed);
        }
        ShiftedLaplacian out;
        out.n    = n;
        out.beta = beta;
        out.sldlt.emplace(std::move(*f));
        return out;
    }

    // Solve (I + β·Δ) x = b. SPD ⇒ the kernel guard is unreachable; a nullopt
    // can only arise from a size mismatch (defensive).
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(std::span<const f64> b) const {
        if (ldlt.has_value())  return ldlt->solve(b);
        if (sldlt.has_value()) return sldlt->solve(b);
        return std::nullopt;   // unfactored default carrier (defensive)
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

    // Generic over the operator carrier: DMatrix -> dense LDLT factor,
    // CsrMatrix -> SparseLdlt (overload resolution on ShiftedLaplacian::make).
    template <typename TOp>
    [[nodiscard]] static std::expected<ImplicitDiffuseStepper, ImplicitError>
    make(const TOp& delta, DiffuseParams p, f64 dt) {
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

    // Generic over the operator carrier (see ImplicitDiffuseStepper::make).
    template <typename TOp>
    [[nodiscard]] static std::expected<ImplicitWaveStepper, ImplicitError>
    make(const TOp& delta, WaveParams p, f64 dt) {
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
