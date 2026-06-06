module;
#include <cmath>
#include <cstddef>
#include <expected>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

export module aleph.sim:helmholtz;

import aleph.math;            // f64
import aleph.types;          // NodeId
import aleph.flow;           // IncrementalLaplacian (the SHARED Δ)
import aleph.linalg.sparse;  // DMatrix, BkLdlt
import :section;

// Frequency-domain Helmholtz operator `(Δ − k²I) φ = source` on the SAME
// IncrementalLaplacian Δ that drives graphics + the wave/heat physics — sound
// on the one shared substrate.
//
// Ported term-for-term from aleph-engine/aleph-audio/src/{helmholtz,source,
// receiver}.rs.
//
// For k² ≈ 0 the operator is positive semi-definite; the flow layer's cached
// IncrementalLaplacian::solve handles it (PSD path reuses that factor, storing
// NOTHING). For k² > 0 the operator is indefinite and we factor with BkLdlt.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust's `DMatrix::{get,set}` map to the C++ `DMatrix::at(i,j)` accessor.
//   * Rust returns `Result<_, HelmholtzError>`; C++ uses std::expected.
//     `solve` returns std::optional, per aleph_flags_isa (no exceptions).
//   * `HelmholtzError::FactorFailed` is a value-less tag: `make` maps ANY
//     BkErrorInfo to it (Rust `map_err(|_| FactorFailed)`, discarding the index).

export namespace aleph::sim {

using aleph::math::f64;
using aleph::types::NodeId;
using aleph::linalg::sparse::BkLdlt;
using aleph::linalg::sparse::DMatrix;

// --- AudioSource / Microphone: the physics↔frequency bridge ----------------

// Speed of sound in air (m/s). Used to map frequency → spatial wavenumber.
inline constexpr f64 kSpeedOfSound = 340.0;

// Monochromatic point source: amplitude `amplitude` at frequency
// `frequency_hz`, anchored to the mesh node `mesh_anchor` (port AudioSource).
struct AudioSource {
    NodeId mesh_anchor;
    f64    frequency_hz;
    f64    amplitude;

    // k² = (2π·f / c)² with c = 340 m/s (air). One multiply, no pow().
    [[nodiscard]] f64 k_squared() const noexcept {
        const f64 k = (2.0 * std::numbers::pi * frequency_hz) / kSpeedOfSound;
        return k * k;
    }

    // RHS vector indexed by `order`. Zero everywhere except at the source's
    // anchor (one-hot); an all-zero vector if the anchor is absent.
    [[nodiscard]] std::vector<f64>
    source_vector(const std::vector<NodeId>& order) const {
        std::vector<f64> b(order.size(), 0.0);
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == mesh_anchor) {
                b[i] = amplitude;
                break;
            }
        }
        return b;
    }
};

// Point microphone receiver (port Microphone).
struct Microphone {
    NodeId mesh_anchor;

    // Sample the audio field φ at the microphone's anchor mesh. Returns 0 if
    // the anchor isn't in `order`.
    [[nodiscard]] f64
    sample(const std::vector<f64>& phi, const std::vector<NodeId>& order) const noexcept {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == mesh_anchor) {
                return (i < phi.size()) ? phi[i] : 0.0;
            }
        }
        return 0.0;
    }
};

// --- HelmholtzOperator ------------------------------------------------------

// Value-less tag (Rust `map_err(|_| FactorFailed)` DISCARDS the BkErrorInfo).
enum class HelmholtzError { FactorFailed };

// Carrier mirrors the Rust `HelmholtzFactor` enum. Psd stores NOTHING (the PSD
// solve reuses the flow's cached factor at solve time); Indefinite holds the
// fresh Bunch-Kaufman factor of `Δ − k²I`.
struct HelmholtzFactor {
    enum class Kind { Psd, Indefinite } kind;
    std::optional<BkLdlt> bk;
};

struct HelmholtzOperator {
    std::size_t     n{0};
    f64             k_squared{0.0};
    HelmholtzFactor factor;

    // Build the operator for a given shift. Reuses the flow's cached LDLᵀ when
    // `k_squared` is essentially zero; otherwise factors `Δ − k²I` via BK
    // (port HelmholtzOperator::new).
    [[nodiscard]] static std::expected<HelmholtzOperator, HelmholtzError>
    make(const aleph::flow::IncrementalLaplacian& flow, f64 k_squared) {
        const std::size_t n = flow.node_order.size();
        if (std::abs(k_squared) < 1e-12) {
            return HelmholtzOperator{n, 0.0,
                                     HelmholtzFactor{HelmholtzFactor::Kind::Psd, std::nullopt}};
        }
        DMatrix h = flow.laplacian;  // copy
        for (std::size_t i = 0; i < n; ++i) {
            h.at(i, i) -= k_squared;
        }
        auto bk = BkLdlt::factorize(h);
        if (!bk) {
            return std::unexpected(HelmholtzError::FactorFailed);
        }
        return HelmholtzOperator{
            n, k_squared,
            HelmholtzFactor{HelmholtzFactor::Kind::Indefinite, std::move(*bk)}};
    }

    // Solve `H · φ = source`. Takes `flow` again (like the Rust `solve(&source,
    // &flow)`) so the PSD branch can reuse the flow's cached factor.
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(const std::vector<f64>& source,
          const aleph::flow::IncrementalLaplacian& flow) const {
        if (source.size() != n) {
            return std::nullopt;
        }
        if (n == 0) {  // empty field — avoid 0/0 in the PSD mean (cf. WaveStepper EmptyField)
            return std::nullopt;
        }
        if (factor.kind == HelmholtzFactor::Kind::Psd) {
            // The flow's solve requires the RHS in range(Δ); project out the
            // constant (kernel) component. Applied UNCONDITIONALLY; return the
            // flow's result UNCHANGED (no post-projection).
            f64 sum = 0.0;
            for (const f64 v : source) {
                sum += v;
            }
            const f64 mean = sum / static_cast<f64>(n);
            std::vector<f64> projected(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                projected[i] = source[i] - mean;
            }
            return flow.solve(std::span<const f64>(projected.data(), projected.size()));
        }
        // Indefinite: solve against the fresh BK factor of Δ − k²I.
        if (!factor.bk) {  // invariant (make() pairs Indefinite with a factor); guard the deref
            return std::nullopt;
        }
        return factor.bk->solve(source);
    }

    // The underlying Helmholtz matrix `Δ − k²I`. Useful for residual /
    // round-trip checks (port HelmholtzOperator::matrix).
    [[nodiscard]] DMatrix matrix(const aleph::flow::IncrementalLaplacian& flow) const {
        DMatrix h = flow.laplacian;
        for (std::size_t i = 0; i < n; ++i) {
            h.at(i, i) -= k_squared;
        }
        return h;
    }
};

}  // namespace aleph::sim
