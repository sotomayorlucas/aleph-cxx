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
using aleph::sim::DiffuseStepper;
using aleph::sim::DiffuseParams;
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
f64 sum_of(const Section<f64>& u) {
    f64 s = 0; for (f64 x : u.data) s += x; return s;
}
// Variance about the (conserved) mean — heat smooths it monotonically.
f64 variance_of(const Section<f64>& u) {
    const std::size_t n = u.size();
    if (n == 0) return 0.0;
    f64 mean = sum_of(u) / static_cast<f64>(n);
    f64 acc = 0;
    for (f64 x : u.data) { const f64 d = x - mean; acc += d * d; }
    return acc / static_cast<f64>(n);
}
// Gershgorin bound on λ_max(Δ): g = max_i Σ_j |Δ_ij|.
f64 gershgorin(const aleph::linalg::sparse::DMatrix& d) {
    f64 g = 0;
    for (std::size_t i = 0; i < d.rows(); ++i) {
        f64 row = 0;
        for (std::size_t j = 0; j < d.cols(); ++j) row += std::fabs(d.at(i, j));
        if (row > g) g = row;
    }
    return g;
}
}  // namespace

TEST_CASE("DiffuseStepper: a spike conserves sum and has non-increasing variance") {
    Path p = make_path(5);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    REQUIRE(L.node_order.size() == 5);

    Section<f64> u = Section<f64>::zeros(L.node_order);
    u.data = {1.0, 0.0, 0.0, 0.0, 0.0};   // an initial spike

    DiffuseStepper s{DiffuseParams{/*alpha=*/0.5}};
    // CFL for explicit heat: dt·α·λmax < 2. Pick dt well inside the bound.
    const f64 lmax = gershgorin(L.matrix);
    REQUIRE(lmax > 0.0);
    const f64 dt = 0.5 / (s.params.alpha * lmax);   // dt·α·λmax = 0.5 < 2
    REQUIRE(dt * s.params.alpha * lmax < 2.0);

    const f64 sum0 = sum_of(u);
    f64 prev_var = variance_of(u);
    for (int i = 0; i < 200; ++i) {
        REQUIRE(s.step(u, L.matrix, dt).has_value());
        // (a) sum conserved — Δ's all-ones kernel: 1ᵀΔ = 0 ⇒ Σu constant.
        CHECK(std::fabs(sum_of(u) - sum0) < 1e-9);
        // (b) variance non-increasing — heat smooths.
        const f64 var = variance_of(u);
        CHECK(var <= prev_var + 1e-12);
        prev_var = var;
    }
    // The spike has actually diffused (variance dropped meaningfully).
    CHECK(prev_var < variance_of(Section<f64>{L.node_order, {1.0,0.0,0.0,0.0,0.0}}));
}

TEST_CASE("DiffuseStepper: deterministic — identical state across two runs") {
    auto run = [] {
        Path p = make_path(6);
        auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
        Section<f64> u = Section<f64>::zeros(L.node_order);
        u.data[0] = 1.0; u.data[3] = -1.0;
        DiffuseStepper s{DiffuseParams{0.5}};
        const f64 lmax = gershgorin(L.matrix);
        const f64 dt = 0.5 / (s.params.alpha * lmax);
        for (int i = 0; i < 100; ++i) (void)s.step(u, L.matrix, dt);
        return u.data;
    };
    std::vector<f64> a = run(), b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i] == b[i]);
}

TEST_CASE("DiffuseStepper: empty field and dim mismatch report errors, no throw") {
    DiffuseStepper s{};
    Section<f64> empty = Section<f64>::zeros(std::vector<NodeId>{});
    auto r1 = s.step(empty, aleph::linalg::sparse::DMatrix::zeros(0, 0), 0.01);
    CHECK(!r1.has_value());
    CHECK(r1.error() == aleph::sim::StepError::EmptyField);

    Section<f64> two = Section<f64>::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    auto r2 = s.step(two, aleph::linalg::sparse::DMatrix::zeros(3, 3), 0.01);
    CHECK(!r2.has_value());
    CHECK(r2.error() == aleph::sim::StepError::DimMismatch);
}
