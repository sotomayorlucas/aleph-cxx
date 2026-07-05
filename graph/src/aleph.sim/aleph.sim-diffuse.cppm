module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

export module aleph.sim:diffuse;

import aleph.math;            // f64
import aleph.linalg.sparse;   // DMatrix
import :section;             // Section, StepError

export namespace aleph::sim {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

struct DiffuseParams { f64 alpha = 1.0; };

// A heat/diffusion stepper over the SAME shared Δ the wave uses, on a single
// Section<f64> — a *different* physics on the *same* abstraction (the payoff).
struct DiffuseStepper {
    DiffuseParams params{};

    // Explicit (forward-Euler) heat step ∂u/∂t = −α·Δu  ⇒  u[i] -= dt·α·(Δu)[i].
    // matvec-only (the shared operator application). Stable for dt·α·λ_max < 2.
    // Generic over the operator carrier (DMatrix or CsrMatrix; matvecs agree to
    // a few ulps and each carrier is byte-deterministic — spec 2026-07-04).
    template <typename TOp>
    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u, const TOp& delta, f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n)
            return std::unexpected(StepError::DimMismatch);
        const std::vector<f64> lap =
            delta.matvec(std::span<const f64>(u.data.data(), n));
        for (std::size_t i = 0; i < n; ++i) {
            u.data[i] -= dt * params.alpha * lap[i];
            if (!std::isfinite(u.data[i])) return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};

}  // namespace aleph::sim
