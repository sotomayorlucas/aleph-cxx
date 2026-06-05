#include "doctest.h"
#include <cmath>
#include <span>
#include <string>
#include <vector>

import aleph.sim;
import aleph.flow;        // build_laplacian, default_weight, WeightedLaplacian
import aleph.graph;       // Graph
import aleph.types;       // NodeId, Mesh, SphereLocal, EdgeKind
import aleph.math;
import aleph.linalg.sparse;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::sim::Section;
using aleph::sim::WaveStepper;
using aleph::sim::WaveParams;
using aleph::math::Vec3;
using aleph::math::f64;

namespace {
struct Path { Graph g; std::vector<NodeId> ids; };
Path make_path(int k) {
    Path p;
    Graph& g = p.g;
    for (int i = 0; i < k; ++i) {
        NodeId m = g.alloc_node_id();
        Mesh mesh{m, std::string("m") + std::to_string(i), 0};
        mesh.geometry = SphereLocal{Vec3{static_cast<float>(i), 0, 0}, 0.4f};
        g.insert_node(std::move(mesh));
        p.ids.push_back(m);
    }
    for (std::size_t i = 0; i + 1 < p.ids.size(); ++i)
        (void)g.add_edge(EdgeKind::Adjacent, p.ids[i], p.ids[i + 1]);
    return p;
}
f64 energy(const Section<f64>& u, const Section<f64>& v,
           const aleph::linalg::sparse::DMatrix& d, f64 c) {
    auto lap = d.matvec(std::span<const f64>(u.data.data(), u.data.size()));
    f64 ke = 0, pe = 0;
    for (std::size_t i = 0; i < u.size(); ++i) { ke += v.data[i]*v.data[i]; pe += u.data[i]*lap[i]; }
    return 0.5*ke + 0.5*c*c*pe;
}
}  // namespace

TEST_CASE("WaveStepper: Δ on a path graph is symmetric with the all-ones kernel") {
    Path p = make_path(3);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    REQUIRE(L.node_order.size() == 3);
    CHECK(L.is_symmetric(1e-12));
    CHECK(L.ones_in_kernel(1e-12));
}

TEST_CASE("WaveStepper: 2-node mode oscillates with bounded energy (damping=1)") {
    Path p = make_path(2);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    Section<f64> u = Section<f64>::zeros(L.node_order);
    Section<f64> v = Section<f64>::zeros(L.node_order);
    u.data[0] = 1.0; u.data[1] = -1.0;           // the λ=2w eigenmode
    WaveStepper s{WaveParams{/*c=*/1.0, /*damping=*/1.0}};
    const f64 dt = 0.01;
    REQUIRE(WaveStepper::cfl_ok(L.matrix, s.params, dt));
    const f64 E0 = energy(u, v, L.matrix, s.params.c);
    bool sign_flipped = false;
    for (int i = 0; i < 4000; ++i) {
        REQUIRE(s.step(u, v, L.matrix, dt).has_value());
        if (u.data[0] < -0.5) sign_flipped = true;
        CHECK(std::fabs(energy(u, v, L.matrix, s.params.c) - E0) < 0.02 * E0 + 1e-9);
    }
    CHECK(sign_flipped);
}

TEST_CASE("WaveStepper: damping<1 makes energy monotone non-increasing") {
    Path p = make_path(4);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    Section<f64> u = Section<f64>::zeros(L.node_order);
    Section<f64> v = Section<f64>::zeros(L.node_order);
    (void)v.add(L.node_order[0], 1.0);
    WaveStepper s{WaveParams{1.0, 0.99}};
    const f64 dt = 0.01;
    f64 prev = energy(u, v, L.matrix, s.params.c);
    for (int i = 0; i < 500; ++i) {
        REQUIRE(s.step(u, v, L.matrix, dt).has_value());
        const f64 e = energy(u, v, L.matrix, s.params.c);
        CHECK(e <= prev + 1e-9);
        prev = e;
    }
}

TEST_CASE("WaveStepper: deterministic — identical trajectory across two runs") {
    auto run = [] {
        Path p = make_path(5);
        auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
        Section<f64> u = Section<f64>::zeros(L.node_order);
        Section<f64> v = Section<f64>::zeros(L.node_order);
        (void)v.add(L.node_order[2], 1.0);
        WaveStepper s{WaveParams{1.0, 0.999}};
        for (int i = 0; i < 200; ++i) (void)s.step(u, v, L.matrix, 0.01);
        return u.data;
    };
    std::vector<f64> a = run(), b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i] == b[i]);
}

TEST_CASE("WaveStepper: empty field and dim mismatch report errors, no throw") {
    Section<f64> empty_u = Section<f64>::zeros(std::vector<NodeId>{});
    Section<f64> empty_v = Section<f64>::zeros(std::vector<NodeId>{});
    WaveStepper s{};
    auto r1 = s.step(empty_u, empty_v, aleph::linalg::sparse::DMatrix::zeros(0, 0), 0.01);
    CHECK(!r1.has_value());
    CHECK(r1.error() == aleph::sim::StepError::EmptyField);

    Section<f64> two_u = Section<f64>::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    Section<f64> two_v = Section<f64>::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    auto r2 = s.step(two_u, two_v, aleph::linalg::sparse::DMatrix::zeros(3, 3), 0.01);
    CHECK(!r2.has_value());
    CHECK(r2.error() == aleph::sim::StepError::DimMismatch);
}
