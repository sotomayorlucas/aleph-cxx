#include "doctest.h"
#include <cmath>
#include <span>
#include <string>
#include <vector>

import aleph.sim;            // HelmholtzOperator, AudioSource, Microphone
import aleph.flow;           // IncrementalLaplacian
import aleph.graph;          // Graph
import aleph.types;          // NodeId, Mesh, SphereLocal, EdgeKind
import aleph.math;           // Vec3, f64
import aleph.linalg.sparse;  // DMatrix

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::flow::IncrementalLaplacian;
using aleph::sim::AudioSource;
using aleph::sim::HelmholtzFactor;
using aleph::sim::HelmholtzOperator;
using aleph::sim::Microphone;
using aleph::math::f64;
using aleph::math::Vec3;
using aleph::linalg::sparse::DMatrix;

namespace {

struct Path {
    Graph               g;
    std::vector<NodeId> ids;
};

// Build a k-node path graph (mirror tests/analytical.rs build_path_graph /
// tests/sim/test_wave.cpp make_path).
Path make_path(int k) {
    Path   p;
    Graph& g = p.g;
    for (int i = 0; i < k; ++i) {
        NodeId m = g.alloc_node_id();
        Mesh   mesh{m, std::string("m") + std::to_string(i), 1};
        mesh.geometry = SphereLocal{Vec3{static_cast<float>(i), 0, 0}, 0.4f};
        g.insert_node(std::move(mesh));
        p.ids.push_back(m);
    }
    for (std::size_t i = 0; i + 1 < p.ids.size(); ++i) {
        (void)g.add_edge(EdgeKind::Adjacent, p.ids[i], p.ids[i + 1]);
    }
    return p;
}

f64 l2_norm(const std::vector<f64>& v) {
    f64 s = 0.0;
    for (const f64 x : v) {
        s += x * x;
    }
    return std::sqrt(s);
}

}  // namespace

// --- branching (port helmholtz_uses_psd_path_at_k_zero / _indefinite_path) --

TEST_CASE("Helmholtz: k²=0 uses the PSD path (reuses the cached factor)") {
    Path p    = make_path(4);
    auto flow = IncrementalLaplacian::from_graph(p.g);
    REQUIRE(flow.has_value());
    auto h = HelmholtzOperator::make(*flow, 0.0);
    REQUIRE(h.has_value());
    CHECK(h->factor.kind == HelmholtzFactor::Kind::Psd);
}

TEST_CASE("Helmholtz: k²>0 uses the indefinite (Bunch-Kaufman) path") {
    Path p    = make_path(4);
    auto flow = IncrementalLaplacian::from_graph(p.g);
    REQUIRE(flow.has_value());
    auto h = HelmholtzOperator::make(*flow, 0.5);
    REQUIRE(h.has_value());
    CHECK(h->factor.kind == HelmholtzFactor::Kind::Indefinite);
}

// --- residual (port helmholtz_solve_consistency_k_positive) -----------------

TEST_CASE("Helmholtz: indefinite solve residual ‖(Δ−k²I)φ − source‖ < 1e-9") {
    Path p    = make_path(5);
    auto flow = IncrementalLaplacian::from_graph(p.g);
    REQUIRE(flow.has_value());
    const f64 k_sq = 0.1;
    auto      h    = HelmholtzOperator::make(*flow, k_sq);
    REQUIRE(h.has_value());

    const std::vector<f64> source = {0.0, 0.0, 1.0, 0.0, 0.0};
    auto                   phi    = h->solve(source, *flow);
    REQUIRE(phi.has_value());

    const DMatrix          hm = h->matrix(*flow);  // Δ − k²I
    const std::vector<f64> hv = hm.matvec(std::span<const f64>(phi->data(), phi->size()));
    std::vector<f64>       residual(hm.rows(), 0.0);
    for (std::size_t i = 0; i < hm.rows(); ++i) {
        residual[i] = hv[i] - source[i];
    }
    const f64 res_norm = l2_norm(residual);
    CHECK(res_norm < 1e-9);
}

// --- PSD projection (C++-only residual; spec §5) ----------------------------

TEST_CASE("Helmholtz: PSD solve is finite and ‖Δφ − projected_source‖ < 1e-9") {
    Path p    = make_path(5);
    auto flow = IncrementalLaplacian::from_graph(p.g);
    REQUIRE(flow.has_value());
    auto h = HelmholtzOperator::make(*flow, 0.0);
    REQUIRE(h.has_value());
    REQUIRE(h->factor.kind == HelmholtzFactor::Kind::Psd);

    const std::vector<f64> source = {1.0, 0.0, 0.0, 0.0, 0.0};
    auto                   phi    = h->solve(source, *flow);
    REQUIRE(phi.has_value());
    for (const f64 v : *phi) {
        CHECK(std::isfinite(v));
    }

    // projected = source − mean(source)
    f64 sum = 0.0;
    for (const f64 v : source) {
        sum += v;
    }
    const f64        mean = sum / static_cast<f64>(source.size());
    std::vector<f64> projected(source.size(), 0.0);
    for (std::size_t i = 0; i < source.size(); ++i) {
        projected[i] = source[i] - mean;
    }

    const std::vector<f64> lap =
        flow->laplacian.matvec(std::span<const f64>(phi->data(), phi->size()));
    std::vector<f64> residual(projected.size(), 0.0);
    for (std::size_t i = 0; i < projected.size(); ++i) {
        residual[i] = lap[i] - projected[i];
    }
    const f64 res_norm = l2_norm(residual);
    CHECK(res_norm < 1e-9);
}

// --- AudioSource / Microphone (port source.rs / receiver.rs in-file tests) --

TEST_CASE("AudioSource: k² is zero at zero frequency") {
    AudioSource s{NodeId{0}, 0.0, 1.0};
    CHECK(s.k_squared() < 1e-12);
}

TEST_CASE("AudioSource: k² grows with frequency") {
    AudioSource lo{NodeId{0}, 100.0, 1.0};
    AudioSource hi{NodeId{0}, 200.0, 1.0};
    CHECK(hi.k_squared() > lo.k_squared());
}

TEST_CASE("AudioSource: source_vector is one-hot at the anchor") {
    AudioSource               s{NodeId{5}, 100.0, 2.5};
    const std::vector<NodeId> order = {NodeId{2}, NodeId{5}, NodeId{9}};
    const std::vector<f64>    v     = s.source_vector(order);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 0.0);
    CHECK(v[1] == 2.5);
    CHECK(v[2] == 0.0);
}

TEST_CASE("AudioSource: source_vector is all-zero when the anchor is absent") {
    AudioSource               s{NodeId{7}, 100.0, 1.0};
    const std::vector<NodeId> order = {NodeId{0}, NodeId{1}};
    const std::vector<f64>    v     = s.source_vector(order);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 0.0);
    CHECK(v[1] == 0.0);
}

TEST_CASE("Microphone: sample reads φ at the anchor index") {
    Microphone                mic{NodeId{7}};
    const std::vector<f64>    phi   = {0.1, 0.4, 0.9};
    const std::vector<NodeId> order = {NodeId{3}, NodeId{7}, NodeId{11}};
    CHECK(std::abs(mic.sample(phi, order) - 0.4) < 1e-12);
}

TEST_CASE("Microphone: sample is 0 when the anchor is absent") {
    Microphone                mic{NodeId{99}};
    const std::vector<f64>    phi   = {0.1};
    const std::vector<NodeId> order = {NodeId{0}};
    CHECK(std::abs(mic.sample(phi, order)) < 1e-12);
}

// --- end-to-end smoke (port audio_smoke.rs) ---------------------------------

TEST_CASE("Helmholtz audio smoke: source → solve → mic level is finite") {
    Path p    = make_path(4);
    auto flow = IncrementalLaplacian::from_graph(p.g);
    REQUIRE(flow.has_value());
    auto h = HelmholtzOperator::make(*flow, 0.0);
    REQUIRE(h.has_value());

    AudioSource            source{p.ids[0], 0.0, 1.0};
    const std::vector<f64> b   = source.source_vector(flow->node_order);
    auto                   phi = h->solve(b, *flow);
    REQUIRE(phi.has_value());

    Microphone mic{p.ids[3]};
    const f64  level = mic.sample(*phi, flow->node_order);
    CHECK(std::isfinite(level));
}

// --- determinism ------------------------------------------------------------

TEST_CASE("Helmholtz: make+solve twice yields byte-identical φ") {
    auto run = []() {
        Path p    = make_path(5);
        auto flow = IncrementalLaplacian::from_graph(p.g);
        REQUIRE(flow.has_value());
        auto h = HelmholtzOperator::make(*flow, 0.1);
        REQUIRE(h.has_value());
        const std::vector<f64> source = {0.0, 0.0, 1.0, 0.0, 0.0};
        auto                   phi    = h->solve(source, *flow);
        REQUIRE(phi.has_value());
        return *phi;
    };
    const std::vector<f64> a = run();
    const std::vector<f64> b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i] == b[i]);
    }
}
