#pragma once

// "Geometry instrument" — a mesh's acoustic resonance spectrum on the SHARED Δ.
//
// Anchor an AudioSource at a mesh node, sweep its frequency, solve the
// frequency-domain Helmholtz system `(Δ − k²I) φ = source` at each step, sample
// the field at a Microphone, and record |response| vs Hz. The peaks are the
// mesh's resonant modes: where `Δ − k²I` is near-singular (k² near an eigenvalue
// of Δ) the response blows up; where the Bunch-Kaufman factor of `Δ − k²I`
// actually fails (exactly singular) HelmholtzOperator::make returns FactorFailed
// and we flag the step `resonant`.
//
// This header is the TESTABLE core: a pure f64 sweep over a fixed step count,
// shared by both `apps/aleph_resonance/main.cpp` and
// `tests/sim/test_resonance.cpp`. No new core numerics — it glues already-tested
// pieces (Helmholtz residual <1e-9).

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

import aleph.flow;   // IncrementalLaplacian (the shared Δ)
import aleph.sim;    // HelmholtzOperator, AudioSource, Microphone
import aleph.types;  // NodeId
import aleph.math;   // f64

namespace aleph::resonance {

using aleph::math::f64;
using aleph::types::NodeId;

// One frequency sample of the sweep.
struct ResonancePoint {
    f64  freq_hz;     // swept driving frequency (Hz)
    f64  k_squared;   // (2π·f / c)² wavenumber-squared shift applied to Δ
    f64  mic_abs;     // |φ at the microphone|; NaN when `resonant`
    bool resonant;    // Δ−k²I was singular (factor failed / no solution)
};

// f_max from the Δ spectrum via a Gershgorin bound on the largest eigenvalue:
// for a graph Laplacian the diagonal equals the off-diagonal absolute row sum,
// so each disc has radius == diagonal and the spectrum is bounded by
// 2·max_i Δ(i,i). Mapping that k²_max back through k = 2π·f / c gives
// f_max = c·sqrt(k²_max) / (2π), so the sweep is guaranteed to cross every
// eigenvalue (= every resonance) of Δ.
[[nodiscard]] inline f64
resonance_f_max_hz(const aleph::flow::IncrementalLaplacian& flow) {
    const std::size_t n = flow.laplacian.rows();
    if (n == 0) return 0.0;  // empty Δ — no spectrum to sweep
    f64 max_diag = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const f64 d = flow.laplacian.at(i, i);
        if (d > max_diag) {
            max_diag = d;
        }
    }
    const f64 k2_max = 2.0 * max_diag;  // Gershgorin upper bound on λ_max(Δ)
    return (aleph::sim::kSpeedOfSound * std::sqrt(k2_max)) /
           (2.0 * std::numbers::pi);
}

// Sweep frequency ∈ [0, f_max_hz] in `steps`+1 inclusive samples. Per step:
// build the Helmholtz operator at that k²; if it cannot be made (singular =
// resonance) or the solve returns nothing, flag `resonant` with mic_abs = NaN;
// otherwise record |Microphone.sample(φ)|.
[[nodiscard]] inline std::vector<ResonancePoint>
resonance_sweep(const aleph::flow::IncrementalLaplacian& flow, NodeId src,
                NodeId mic, f64 f_max_hz, int steps) {
    std::vector<ResonancePoint> out;
    if (steps < 1) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(steps) + 1);

    const f64                  nan      = std::numeric_limits<f64>::quiet_NaN();
    const aleph::sim::Microphone receiver{mic};

    for (int i = 0; i <= steps; ++i) {
        const f64 freq = (f_max_hz * static_cast<f64>(i)) / static_cast<f64>(steps);
        const aleph::sim::AudioSource a{src, freq, 1.0};
        const f64 k2 = a.k_squared();

        auto op = aleph::sim::HelmholtzOperator::make(flow, k2);
        if (!op) {
            out.push_back(ResonancePoint{freq, k2, nan, true});
            continue;
        }
        auto phi = op->solve(a.source_vector(flow.node_order), flow);
        if (!phi) {
            out.push_back(ResonancePoint{freq, k2, nan, true});
            continue;
        }
        const f64 mic_abs = std::abs(receiver.sample(*phi, flow.node_order));
        out.push_back(ResonancePoint{freq, k2, mic_abs, false});
    }
    return out;
}

}  // namespace aleph::resonance
