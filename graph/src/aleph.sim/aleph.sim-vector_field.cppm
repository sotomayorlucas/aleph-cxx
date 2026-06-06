module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

export module aleph.sim:vector_field;

import aleph.math;            // f64, f32, Vec3
import aleph.linalg.sparse;   // DMatrix
import :section;             // Section, StepError

export namespace aleph::sim {

using aleph::math::f64;
using aleph::math::f32;
using aleph::math::Vec3;
using aleph::linalg::sparse::DMatrix;

struct VectorDiffuseParams { f64 alpha = 1.0; };

// A vector heat/diffusion stepper over the SAME shared Δ the wave/scalar-heat use,
// on a single Section<Vec3> — the scalar DiffuseStepper physics lifted to a vector
// field by applying the scalar Δ COMPONENT-WISE (x, y, z evolve independently; no
// cross-component coupling). Matvec-only; f64 matvec → f32 component write-back
// (Vec3 components are f32). The one-substrate payoff: the same factorized Δ + the
// Section abstraction now drive a vector field, not just scalars.
struct VectorDiffuseStepper {
    VectorDiffuseParams params{};

    // Explicit (forward-Euler) heat per component: u[i].c -= dt·α·(Δ u_c)[i] for
    // c ∈ {x,y,z} in fixed order. Stable for dt·α·λ_max < 2.
    [[nodiscard]] std::expected<void, StepError>
    step(Section<Vec3>& u, const DMatrix& delta, f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n)
            return std::unexpected(StepError::DimMismatch);

        std::vector<f64> comp(n);  // f64 scratch reused across the 3 component blocks.

        // comp[] holds the f64 snapshot of the component, so the write-back reads it
        // (not u.data[i].c again) — the scratch's purpose + no redundant re-read/cast.
        // --- X ---
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].x);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].x = static_cast<f32>(comp[i] - dt * params.alpha * lap[i]); }
        // --- Y ---
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].y);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].y = static_cast<f32>(comp[i] - dt * params.alpha * lap[i]); }
        // --- Z ---
        for (std::size_t i = 0; i < n; ++i) comp[i] = static_cast<f64>(u.data[i].z);
        { const std::vector<f64> lap = delta.matvec(std::span<const f64>(comp.data(), n));
          for (std::size_t i = 0; i < n; ++i)
              u.data[i].z = static_cast<f32>(comp[i] - dt * params.alpha * lap[i]); }

        // Single post-update non-finite pass over all 3 components.
        for (std::size_t i = 0; i < n; ++i)
            if (!std::isfinite(u.data[i].x) || !std::isfinite(u.data[i].y) || !std::isfinite(u.data[i].z))
                return std::unexpected(StepError::NonFinite);
        return {};
    }
};

}  // namespace aleph::sim
