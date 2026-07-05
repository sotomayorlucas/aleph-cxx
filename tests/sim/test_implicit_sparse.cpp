// Sparse factors for the implicit path (spec 2026-07-04, second slice).
//
// ShiftedLaplacian gains a CsrMatrix make() that shifts I + beta*Delta in
// O(nnz) and factors it with SparseLdlt — the implicit-stepping pipeline is
// then sparse end-to-end (assembly -> shift -> factor -> solve). The FP
// contract mirrors the matvec finding: dense-factor and sparse-factor solves
// agree within solver roundoff (NOT bitwise — different elimination order),
// and each path is byte-deterministic against itself.

#include "doctest.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

import aleph.sim;
import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.math;
import aleph.linalg.sparse;

using namespace aleph::types;
using aleph::flow::build_laplacian_bounded;
using aleph::flow::build_laplacian_bounded_sparse;
using aleph::flow::default_weight;
using aleph::graph::Graph;
using aleph::linalg::sparse::CsrMatrix;
using aleph::math::f64;
using aleph::math::Vec3;
using aleph::sim::DiffuseParams;
using aleph::sim::ImplicitDiffuseStepper;
using aleph::sim::ImplicitError;
using aleph::sim::ImplicitWaveStepper;
using aleph::sim::Section;
using aleph::sim::ShiftedLaplacian;
using aleph::sim::WaveParams;

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
f64 variance_of(const Section<f64>& u) {
    const std::size_t n = u.size();
    if (n == 0) return 0.0;
    f64 mean = sum_of(u) / static_cast<f64>(n);
    f64 acc = 0;
    for (f64 x : u.data) { const f64 d = x - mean; acc += d * d; }
    return acc / static_cast<f64>(n);
}
f64 max_abs(const Section<f64>& u) {
    f64 m = 0; for (f64 x : u.data) m = std::max(m, std::fabs(x)); return m;
}
f64 gershgorin_csr(const CsrMatrix& d) {
    const auto& rp = d.row_ptr();
    const auto& vs = d.values();
    f64 g = 0;
    for (std::size_t i = 0; i < d.rows(); ++i) {
        f64 row = 0;
        for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) row += std::fabs(vs[k]);
        if (row > g) g = row;
    }
    return g;
}
}  // namespace

// Sparse-factor diffuse: uniform fixed point, huge-dt stability, mass
// conservation, monotone smoothing — the dense suite's oracles on the CSR
// carrier.
TEST_CASE("implicit-sp: diffuse on the sparse factor — stability oracles") {
    Path p = make_path(8);
    const auto sp = build_laplacian_bounded_sparse(p.g, default_weight);

    const f64 lam = gershgorin_csr(sp.matrix);
    const f64 dt  = 100.0 * 2.0 / lam;

    auto st = ImplicitDiffuseStepper::make(sp.matrix, DiffuseParams{1.0}, dt);
    REQUIRE(st.has_value());

    // Uniform fixed point.
    {
        Section<f64> u = Section<f64>::zeros(sp.node_order);
        for (auto& x : u.data) x = 3.25;
        REQUIRE(st->step(u).has_value());
        for (const f64 x : u.data) CHECK(std::fabs(x - 3.25) < 1e-12);
    }
    // Spike: bounded, mass-conserving, variance-decreasing at 100x CFL.
    {
        Section<f64> u = Section<f64>::zeros(sp.node_order);
        u.data[0] = 1.0;
        const f64 mass0 = sum_of(u);
        f64 var_prev = variance_of(u);
        for (int s = 0; s < 50; ++s) {
            REQUIRE(st->step(u).has_value());
            const f64 var = variance_of(u);
            CHECK(var <= var_prev + 1e-12);
            var_prev = var;
        }
        CHECK(std::fabs(sum_of(u) - mass0) < 1e-9);
        CHECK(max_abs(u) <= 1.0);
        CHECK(variance_of(u) < 1e-6);
    }
}

// Dense-factor vs sparse-factor agreement: single step <= 1e-9 per entry,
// 32-step trajectory <= 1e-8; the sparse trajectory is bitwise reproducible.
TEST_CASE("implicit-sp: dense vs sparse factor agree; sparse reproducible") {
    Path p = make_path(8);
    const auto de = build_laplacian_bounded(p.g, default_weight);
    const auto sp = build_laplacian_bounded_sparse(p.g, default_weight);

    const f64 dt = 0.7;
    auto std_ = ImplicitDiffuseStepper::make(de.matrix, DiffuseParams{}, dt);
    auto stsp = ImplicitDiffuseStepper::make(sp.matrix, DiffuseParams{}, dt);
    REQUIRE(std_.has_value());
    REQUIRE(stsp.has_value());

    auto run = [&](const auto& st, Section<f64>& u, int steps) {
        u = Section<f64>::zeros(de.node_order);
        u.data[2] = 2.0;
        for (int s = 0; s < steps; ++s) REQUIRE(st.step(u).has_value());
    };
    Section<f64> u1, us1, us2;
    run(*std_, u1, 1);
    run(*stsp, us1, 1);
    for (std::size_t i = 0; i < u1.size(); ++i) {
        CAPTURE(i);
        CHECK(std::fabs(u1.data[i] - us1.data[i]) <= 1e-9);
    }
    run(*std_, u1, 32);
    run(*stsp, us1, 32);
    run(*stsp, us2, 32);
    for (std::size_t i = 0; i < u1.size(); ++i) {
        CAPTURE(i);
        CHECK(std::fabs(u1.data[i] - us1.data[i]) <= 1e-8);
        CHECK(us1.data[i] == us2.data[i]);   // bitwise self-reproducible
    }
}

// Wave on the sparse factor: uniform zero-velocity fixed point + bounded
// zero-momentum kick far past the explicit CFL.
TEST_CASE("implicit-sp: wave on the sparse factor") {
    Path p = make_path(8);
    const auto sp = build_laplacian_bounded_sparse(p.g, default_weight);

    const f64 lam = gershgorin_csr(sp.matrix);
    const f64 dt  = 10.0 * 2.0 / std::sqrt(lam);

    auto st = ImplicitWaveStepper::make(sp.matrix, WaveParams{}, dt);
    REQUIRE(st.has_value());

    // Fixed point.
    {
        Section<f64> u = Section<f64>::zeros(sp.node_order);
        Section<f64> v = Section<f64>::zeros(sp.node_order);
        for (auto& x : u.data) x = -1.75;
        REQUIRE(st->step(u, v).has_value());
        for (const f64 x : u.data) CHECK(std::fabs(x + 1.75) < 1e-12);
        for (const f64 x : v.data) CHECK(std::fabs(x) < 1e-12);
    }
    // Zero-momentum kick: bounded, flattening.
    {
        Section<f64> u = Section<f64>::zeros(sp.node_order);
        Section<f64> v = Section<f64>::zeros(sp.node_order);
        v.data[3] = 1.5;
        v.data[4] = -1.5;
        f64 peak = 0.0;
        for (int s = 0; s < 200; ++s) {
            REQUIRE(st->step(u, v).has_value());
            peak = std::max(peak, std::max(max_abs(u), max_abs(v)));
        }
        CHECK(std::isfinite(peak));
        CHECK(peak < 10.0);
        CHECK(variance_of(u) < 1e-6);
        CHECK(variance_of(v) < 1e-6);
    }
}

// make(CsrMatrix) contract: a diagonal-less CSR still factors (the shift
// inserts the +1 diagonal); a non-symmetric CSR fails; bad beta/dt rejected.
TEST_CASE("implicit-sp: CSR make contract") {
    // 3-node path Laplacian WITHOUT stored diagonals: rows {(-1@1)}, ...
    {
        std::vector<std::size_t> rp{0, 1, 3, 4};
        std::vector<std::size_t> ci{1, 0, 2, 1};
        std::vector<f64>         vs{-1.0, -1.0, -1.0, -1.0};
        const CsrMatrix a = CsrMatrix::from_parts(3, 3, rp, ci, vs);
        auto op = ShiftedLaplacian::make(a, 0.5);
        REQUIRE(op.has_value());
        // (I + 0.5*A) x = b solvable; diagonal rows exist.
        const std::vector<f64> b{1.0, 2.0, 3.0};
        const auto x = op->solve(std::span<const f64>(b));
        REQUIRE(x.has_value());
        for (const f64 xi : *x) CHECK(std::isfinite(xi));
    }
    // Non-symmetric -> FactorFailed.
    {
        std::vector<std::size_t> rp{0, 1, 1};
        std::vector<std::size_t> ci{1};
        std::vector<f64>         vs{-1.0};
        const CsrMatrix a = CsrMatrix::from_parts(2, 2, rp, ci, vs);
        auto op = ShiftedLaplacian::make(a, 1.0);
        REQUIRE_FALSE(op.has_value());
        CHECK(op.error() == ImplicitError::FactorFailed);
    }
    // beta < 0 -> InvalidShift; dt <= 0 -> InvalidShift (templated make).
    {
        const CsrMatrix a = CsrMatrix::empty();
        auto op = ShiftedLaplacian::make(a, -1.0);
        REQUIRE_FALSE(op.has_value());
        CHECK(op.error() == ImplicitError::InvalidShift);
    }
}
