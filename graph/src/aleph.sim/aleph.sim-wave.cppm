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

// Error taxonomy for the stepper. NOTE: `CflViolation` is NOT emitted by `step`
// (which would need an O(n²) Gershgorin scan every frame); it is the code a caller
// returns when its own `cfl_ok` pre-check fails. `step` instead catches a blow-up
// post-hoc via `NonFinite`.
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
    //
    // CONTRACT: on a StepError::NonFinite return the field is left PARTIALLY
    // updated (the diverging entry and everything before it stepped, the rest
    // stale) — discard it; do not re-step. The editor controller honours this by
    // bailing before it re-bakes/render and by re-zeroing on the next enable_sim.
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
            // Damp-then-force ordering is a faithful port of the Rust reference
            // (aleph-playground wave.rs: `phi_dot = phi_dot*damp + dt*c²*lap`);
            // it is intentional. (Force-then-damp is an equally valid convention
            // differing only by scaling the force term by `damping` ≈ 0.1%/step.)
            field.phi_dot[i] = params.damping * field.phi_dot[i] - dt * c2 * lap[i];
            field.phi[i]    += dt * field.phi_dot[i];
            if (!std::isfinite(field.phi[i]) || !std::isfinite(field.phi_dot[i]))
                return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};

}  // namespace aleph::sim
