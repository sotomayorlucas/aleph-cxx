// aleph_resonance — the "geometry instrument" demo.
//
// Build a small grid mesh, lower it to the SHARED Δ (IncrementalLaplacian), then
// drive an AudioSource at one corner across a frequency sweep, solve the
// Helmholtz system `(Δ − k²I)φ = source` at each step, and sample a Microphone at
// the opposite corner. The |response|-vs-Hz curve is the mesh's acoustic
// resonance spectrum; its peaks are the resonant modes (k² near an eigenvalue of
// Δ — the same Δ that renders the mesh gives its sound).
//
// Output (CLI `aleph_resonance <out_prefix>`, default /tmp/aleph_resonance):
//   <prefix>.csv  — freq_hz,k_squared,mic_abs,resonant
//   <prefix>.ppm  — a 640x240 log-y line-plot of mic_abs vs freq (resonant
//                   columns marked in a distinct colour)
// and the top ~5 peak frequencies printed to stdout.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "resonance.hpp"

import aleph.graph;  // Graph
import aleph.types;  // Mesh, NodeId, EdgeKind
import aleph.flow;   // IncrementalLaplacian
import aleph.math;   // f64

namespace {

using aleph::math::f64;
using aleph::resonance::ResonancePoint;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

// R x R grid of Mesh nodes joined by 4-neighbour Adjacent edges
// (mirrors tests/flow/test_mv_localization.cpp:make_grid). ids[r][c].
struct Grid {
    aleph::graph::Graph              g;
    std::vector<std::vector<NodeId>> ids;
};

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
                (void)grid.g.add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i][j + 1]);
            }
            if (i + 1 < R) {
                (void)grid.g.add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i + 1][j]);
            }
        }
    }
    return grid;
}

// Clamp a byte channel into [0,255].
unsigned char clamp_byte(f64 v) {
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return static_cast<unsigned char>(v + 0.5);
}

// Render the spectrum to a W x H PPM (P6) log-y line plot. mic_abs is plotted on
// a log scale (peaks read clearly); resonant columns are drawn as a full-height
// magenta bar.
bool write_ppm(const std::string& path, const std::vector<ResonancePoint>& sweep,
               std::size_t W, std::size_t H) {
    // log-y range over the finite (non-resonant) samples.
    f64 lo = 0.0, hi = 0.0;
    bool have = false;
    for (const auto& p : sweep) {
        if (p.resonant || !std::isfinite(p.mic_abs) || p.mic_abs <= 0.0) {
            continue;
        }
        const f64 l = std::log10(p.mic_abs);
        if (!have) {
            lo = hi = l;
            have    = true;
        } else {
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
    }
    if (!have) {
        lo = 0.0;
        hi = 1.0;
    }
    if (hi - lo < 1e-9) {
        hi = lo + 1.0;  // avoid a zero-height plot
    }

    std::vector<unsigned char> img(W * H * 3, 0);  // black background
    auto put = [&](std::size_t x, std::size_t y, unsigned char r, unsigned char gr,
                   unsigned char b) {
        if (x >= W || y >= H) return;
        const std::size_t idx = (y * W + x) * 3;
        img[idx + 0]          = r;
        img[idx + 1]          = gr;
        img[idx + 2]          = b;
    };

    const std::size_t n = sweep.size();
    for (std::size_t x = 0; x < W; ++x) {
        // map plot column -> sweep index
        const std::size_t si =
            (n <= 1) ? 0
                     : (x * (n - 1)) / (W - 1 == 0 ? 1 : (W - 1));
        const ResonancePoint& p = sweep[si];

        if (p.resonant || !std::isfinite(p.mic_abs) || p.mic_abs <= 0.0) {
            // resonance marker: full-height magenta bar.
            for (std::size_t y = 0; y < H; ++y) {
                put(x, y, 255, 0, 200);
            }
            continue;
        }
        const f64 l    = std::log10(p.mic_abs);
        const f64 frac = (l - lo) / (hi - lo);  // 0..1
        std::size_t bar =
            static_cast<std::size_t>(frac * static_cast<f64>(H - 1) + 0.5);
        if (bar >= H) bar = H - 1;
        // column from baseline up to `bar`, in cyan->green by height.
        for (std::size_t yy = 0; yy <= bar; ++yy) {
            const std::size_t y = H - 1 - yy;  // bottom-up
            const f64 t         = static_cast<f64>(yy) / static_cast<f64>(H - 1);
            put(x, y, clamp_byte(40.0 * (1.0 - t)), clamp_byte(120.0 + 135.0 * t),
                clamp_byte(180.0 * (1.0 - t)));
        }
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "aleph_resonance: cannot open %s for writing\n",
                     path.c_str());
        return false;
    }
    std::fprintf(f, "P6\n%zu %zu\n255\n", W, H);
    std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string prefix = (argc > 1) ? argv[1] : "/tmp/aleph_resonance";

    const std::size_t R    = 6;
    Grid              grid = make_grid(R);

    auto flow_e = aleph::flow::IncrementalLaplacian::from_graph(grid.g);
    if (!flow_e) {
        std::fprintf(stderr,
                     "aleph_resonance: IncrementalLaplacian::from_graph failed\n");
        return 1;
    }
    const aleph::flow::IncrementalLaplacian& flow = *flow_e;

    // Drive at one corner, listen at the opposite corner.
    const NodeId src = grid.ids[0][0];
    const NodeId mic = grid.ids[R - 1][R - 1];

    const f64 f_max = aleph::resonance::resonance_f_max_hz(flow);
    const int steps = 400;

    const std::vector<ResonancePoint> sweep =
        aleph::resonance::resonance_sweep(flow, src, mic, f_max, steps);
    if (sweep.empty()) {
        std::fprintf(stderr, "aleph_resonance: empty sweep\n");
        return 1;
    }

    // --- CSV ---------------------------------------------------------------
    const std::string csv_path = prefix + ".csv";
    std::FILE*        csv       = std::fopen(csv_path.c_str(), "wb");
    if (!csv) {
        std::fprintf(stderr, "aleph_resonance: cannot open %s\n", csv_path.c_str());
        return 1;
    }
    std::fprintf(csv, "freq_hz,k_squared,mic_abs,resonant\n");
    for (const auto& p : sweep) {
        std::fprintf(csv, "%.6f,%.9f,%.9g,%d\n", p.freq_hz, p.k_squared, p.mic_abs,
                     p.resonant ? 1 : 0);
    }
    std::fclose(csv);

    // --- PPM line-plot -----------------------------------------------------
    const std::string ppm_path = prefix + ".ppm";
    if (!write_ppm(ppm_path, sweep, 640, 240)) {
        return 1;
    }

    // --- top peaks ---------------------------------------------------------
    // A peak = a local maximum of mic_abs over finite samples, OR a resonant
    // marker (singular Δ−k²I). Score resonant points as +inf so they sort first.
    struct Peak {
        f64  freq;
        f64  score;
        bool resonant;
    };
    std::vector<Peak> peaks;
    const std::size_t n = sweep.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& p = sweep[i];
        if (p.resonant || !std::isfinite(p.mic_abs)) {
            peaks.push_back(Peak{p.freq_hz,
                                 std::numeric_limits<f64>::infinity(), true});
            continue;
        }
        const f64 prev = (i > 0 && std::isfinite(sweep[i - 1].mic_abs))
                             ? sweep[i - 1].mic_abs
                             : 0.0;
        const f64 next = (i + 1 < n && std::isfinite(sweep[i + 1].mic_abs))
                             ? sweep[i + 1].mic_abs
                             : 0.0;
        if (p.mic_abs >= prev && p.mic_abs >= next && p.mic_abs > 0.0) {
            peaks.push_back(Peak{p.freq_hz, p.mic_abs, false});
        }
    }
    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.score > b.score; });

    std::printf("aleph_resonance: grid %zux%zu, %d-step sweep over [0, %.1f] Hz\n",
                R, R, steps, f_max);
    std::printf("  src=corner(0,0) mic=corner(%zu,%zu)\n", R - 1, R - 1);
    std::printf("  top resonant modes (peaks of |response|):\n");
    const std::size_t topk = std::min<std::size_t>(5, peaks.size());
    for (std::size_t i = 0; i < topk; ++i) {
        if (peaks[i].resonant) {
            std::printf("    ~%.1f Hz   (Δ−k²I singular — exact resonance)\n",
                        peaks[i].freq);
        } else {
            std::printf("    ~%.1f Hz   (|response| = %.4g)\n", peaks[i].freq,
                        peaks[i].score);
        }
    }
    std::printf("  wrote %s + %s\n", csv_path.c_str(), ppm_path.c_str());
    return 0;
}
