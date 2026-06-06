// Tier-1 gate for the aleph_resonance demo's testable core (resonance_sweep).
//
// The demo computes a mesh's acoustic resonance spectrum on the SHARED Δ:
// AudioSource at one node, frequency sweep, Helmholtz solve `(Δ−k²I)φ=source` at
// each k², Microphone sample. This suite proves the sweep behaves as a spectrum
// rather than a flat line, that the response is finite away from resonances, and
// that it is deterministic.

#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "../../apps/aleph_resonance/resonance.hpp"

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.math;

using namespace aleph::types;
using aleph::flow::IncrementalLaplacian;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::resonance::ResonancePoint;
using aleph::resonance::resonance_f_max_hz;
using aleph::resonance::resonance_sweep;

namespace {

struct Grid {
    Graph                            g;
    std::vector<std::vector<NodeId>> ids;
};

// R x R grid of Mesh nodes joined by 4-neighbour Adjacent edges
// (mirror tests/flow/test_mv_localization.cpp:make_grid).
Grid make_grid(std::size_t R) {
    Grid grid;
    grid.ids.assign(R, std::vector<NodeId>(R));
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            const NodeId id = grid.g.alloc_node_id();
            grid.g.insert_node(Mesh{
                id, std::string("m") + std::to_string(i) + "_" + std::to_string(j),
                1});
            grid.ids[i][j] = id;
        }
    }
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            if (j + 1 < R) {
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i][j + 1])
                            .has_value());
            }
            if (i + 1 < R) {
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i + 1][j])
                            .has_value());
            }
        }
    }
    return grid;
}

f64 median_of(std::vector<f64> v) {
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size();
    if (m == 0) return 0.0;
    return (m % 2 == 1) ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]);
}

}  // namespace

TEST_CASE("resonance: low-frequency response is finite (the PSD/Green's baseline)") {
    Grid grid = make_grid(5);
    auto flow = IncrementalLaplacian::from_graph(grid.g);
    REQUIRE(flow.has_value());

    const NodeId src = grid.ids[0][0];
    const NodeId mic = grid.ids[4][4];
    const f64    fmax = resonance_f_max_hz(*flow);
    const auto   sweep = resonance_sweep(*flow, src, mic, fmax, 200);

    REQUIRE(sweep.size() == 201u);
    // The k²≈0 sample (step 0) uses the PSD path: finite, non-resonant.
    CHECK(sweep.front().freq_hz == doctest::Approx(0.0));
    CHECK_FALSE(sweep.front().resonant);
    CHECK(std::isfinite(sweep.front().mic_abs));
}

TEST_CASE("resonance: spectrum is non-trivial (a clear peak, not a flat line)") {
    Grid grid = make_grid(6);
    auto flow = IncrementalLaplacian::from_graph(grid.g);
    REQUIRE(flow.has_value());

    const NodeId src = grid.ids[0][0];
    const NodeId mic = grid.ids[5][5];
    const f64    fmax = resonance_f_max_hz(*flow);
    REQUIRE(fmax > 0.0);
    const auto sweep = resonance_sweep(*flow, src, mic, fmax, 400);
    REQUIRE_FALSE(sweep.empty());

    std::vector<f64> finite;
    f64              max_abs = 0.0;
    for (const auto& p : sweep) {
        if (!p.resonant) {
            REQUIRE(std::isfinite(p.mic_abs));  // every non-resonant sample finite
            finite.push_back(p.mic_abs);
            max_abs = std::max(max_abs, p.mic_abs);
        }
    }
    REQUIRE(finite.size() > 10u);
    const f64 med = median_of(finite);
    REQUIRE(med > 0.0);
    const f64 ratio = max_abs / med;
    INFO("max/median ratio = " << ratio);
    CHECK(ratio > 5.0);  // a clear resonance peak exists
}

TEST_CASE("resonance: resonant markers sit next to a large response") {
    Grid grid = make_grid(6);
    auto flow = IncrementalLaplacian::from_graph(grid.g);
    REQUIRE(flow.has_value());

    const NodeId src = grid.ids[0][0];
    const NodeId mic = grid.ids[5][5];
    const f64    fmax = resonance_f_max_hz(*flow);
    const auto   sweep = resonance_sweep(*flow, src, mic, fmax, 400);

    // Baseline (median) over the finite samples.
    std::vector<f64> finite;
    for (const auto& p : sweep) {
        if (!p.resonant) finite.push_back(p.mic_abs);
    }
    const f64 med = median_of(finite);

    // If any step is flagged resonant, at least one immediate neighbour should
    // show an amplified (well-above-median) finite response — resonances cluster
    // with peaks, they are not isolated noise.
    const std::size_t n = sweep.size();
    bool              any_resonant = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!sweep[i].resonant) continue;
        any_resonant = true;
        f64 best     = 0.0;
        if (i > 0 && std::isfinite(sweep[i - 1].mic_abs)) {
            best = std::max(best, sweep[i - 1].mic_abs);
        }
        if (i + 1 < n && std::isfinite(sweep[i + 1].mic_abs)) {
            best = std::max(best, sweep[i + 1].mic_abs);
        }
        CHECK(best > 2.0 * med);
    }
    INFO("any resonant marker present = " << any_resonant);
    // Not asserting any_resonant: exact singularity is grid-dependent. The
    // non-trivial-peak test already guarantees the spectrum shows modes.
}

TEST_CASE("resonance: the sweep is deterministic (two runs element-equal)") {
    Grid grid = make_grid(6);
    auto flow = IncrementalLaplacian::from_graph(grid.g);
    REQUIRE(flow.has_value());

    const NodeId src = grid.ids[0][0];
    const NodeId mic = grid.ids[5][5];
    const f64    fmax = resonance_f_max_hz(*flow);

    const auto a = resonance_sweep(*flow, src, mic, fmax, 400);
    const auto b = resonance_sweep(*flow, src, mic, fmax, 400);

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].freq_hz == b[i].freq_hz);      // exact: same f64 arithmetic
        CHECK(a[i].k_squared == b[i].k_squared);
        CHECK(a[i].resonant == b[i].resonant);
        if (a[i].resonant) {
            CHECK(std::isnan(a[i].mic_abs));
            CHECK(std::isnan(b[i].mic_abs));
        } else {
            CHECK(a[i].mic_abs == b[i].mic_abs);  // bit-identical
        }
    }
}
