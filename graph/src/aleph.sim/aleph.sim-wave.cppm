module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

export module aleph.sim:wave;

import aleph.math;            // f64
import aleph.linalg.sparse;   // DMatrix
import :field;

export namespace aleph::sim {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

struct WaveParams {
    f64 c       = 1.0;     // wave speed
    f64 damping = 0.999;   // per-step multiplicative velocity damp
};

enum class StepError { EmptyField, DimMismatch, CflViolation, NonFinite };

struct WaveStepper {
    WaveParams params{};

    // Conservative CFL guard via the Gershgorin radius g = max_i sum_j |Δ_ij|
    // (bounds λ_max(Δ)); explicit scheme stable for c²·dt²·λ_max < 4.
    [[nodiscard]] static bool
    cfl_ok(const DMatrix& delta, const WaveParams& p, f64 dt) noexcept {
        const std::size_t n = delta.rows();
        f64 g = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            f64 row = 0.0;
            for (std::size_t j = 0; j < delta.cols(); ++j) row += std::fabs(delta.at(i, j));
            if (row > g) g = row;
        }
        return p.c * p.c * dt * dt * g < 4.0;
    }

    // One explicit symplectic-Euler ("Verlet") sub-step of φ̈ = −c²Δφ.
    [[nodiscard]] std::expected<void, StepError>
    step(ScalarField& field, const DMatrix& delta, f64 dt) const noexcept {
        const std::size_t n = field.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n)
            return std::unexpected(StepError::DimMismatch);

        // lap = Δ·φ  (the SHARED operator application; matvec only)
        const std::vector<f64> lap =
            delta.matvec(std::span<const f64>(field.phi.data(), n));
        const f64 c2 = params.c * params.c;
        for (std::size_t i = 0; i < n; ++i) {
            field.phi_dot[i] = params.damping * field.phi_dot[i] - dt * c2 * lap[i];
            field.phi[i]    += dt * field.phi_dot[i];
            if (!std::isfinite(field.phi[i]) || !std::isfinite(field.phi_dot[i]))
                return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};

}  // namespace aleph::sim
