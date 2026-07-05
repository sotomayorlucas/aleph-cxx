module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

export module aleph.sim:wave;

import aleph.math;            // f64
import aleph.linalg.sparse;   // DMatrix
import :section;

export namespace aleph::sim {

using aleph::math::f64;
using aleph::linalg::sparse::CsrMatrix;
using aleph::linalg::sparse::DMatrix;

struct WaveParams {
    f64 c       = 1.0;     // wave speed
    f64 damping = 0.999;   // per-step multiplicative velocity damp
};

// StepError lives in :section (shared by wave + diffuse).

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

    // Sparse-carrier overload: the row abs-sum over stored entries equals the
    // dense row's bitwise (adding |0.0| is an exact no-op).
    [[nodiscard]] static bool
    cfl_ok(const CsrMatrix& delta, const WaveParams& p, f64 dt) noexcept {
        const auto& rp = delta.row_ptr();
        const auto& vs = delta.values();
        f64 g = 0.0;
        for (std::size_t i = 0; i < delta.rows(); ++i) {
            f64 row = 0.0;
            for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) row += std::fabs(vs[k]);
            if (row > g) g = row;
        }
        return p.c * p.c * dt * dt * g < 4.0;
    }

    // One explicit symplectic-Euler ("Verlet") sub-step of φ̈ = −c²Δφ, on two
    // sections: u (displacement φ) and v (velocity φ̇).
    //
    // CONTRACT: on a StepError::NonFinite return the sections are left PARTIALLY
    // updated (the diverging entry and everything before it stepped, the rest
    // stale) — discard them; do not re-step. The editor controller honours this by
    // bailing before it re-bakes/render and by re-zeroing on the next enable_sim.
    // Generic over the operator carrier (dense DMatrix or CsrMatrix — both
    // expose rows/cols/matvec; matvecs agree to a few ulps per entry and each
    // carrier is byte-deterministic, spec 2026-07-04). Call sites unchanged.
    template <typename TOp>
    [[nodiscard]] std::expected<void, StepError>
    step(Section<f64>& u, Section<f64>& v, const TOp& delta, f64 dt) const noexcept {
        const std::size_t n = u.size();
        if (n == 0) return std::unexpected(StepError::EmptyField);
        if (delta.rows() != n || delta.cols() != n || v.size() != n)
            return std::unexpected(StepError::DimMismatch);

        // lap = Δ·u  (the SHARED operator application; matvec only)
        const std::vector<f64> lap =
            delta.matvec(std::span<const f64>(u.data.data(), n));
        const f64 c2 = params.c * params.c;
        for (std::size_t i = 0; i < n; ++i) {
            // Damp-then-force ordering is a faithful port of the Rust reference
            // (aleph-playground wave.rs: `phi_dot = phi_dot*damp + dt*c²*lap`);
            // it is intentional. (Force-then-damp is an equally valid convention
            // differing only by scaling the force term by `damping` ≈ 0.1%/step.)
            v.data[i] = params.damping * v.data[i] - dt * c2 * lap[i];
            u.data[i] += dt * v.data[i];
            if (!std::isfinite(u.data[i]) || !std::isfinite(v.data[i]))
                return std::unexpected(StepError::NonFinite);
        }
        return {};
    }
};

}  // namespace aleph::sim
