// Implicit (unconditionally stable) steppers on the shared Δ (spec 2026-07-03).
//
// Backward-Euler heat and wave solve (I + β·Δ) u⁺ = rhs on the SAME
// bounded-κ_R Laplacian the explicit steppers matvec against. The payoff test
// is #2: a dt far beyond the explicit CFL bound where DiffuseStepper diverges
// and ImplicitDiffuseStepper stays finite, mass-conserving, and smoothing.

#include "doctest.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

import aleph.sim;
import aleph.flow;        // build_laplacian_bounded, default_weight
import aleph.graph;       // Graph
import aleph.types;       // NodeId, Mesh, SphereLocal, EdgeKind
import aleph.math;
import aleph.linalg.sparse;

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::linalg::sparse::DMatrix;
using aleph::math::f64;
using aleph::math::Vec3;
using aleph::sim::DiffuseParams;
using aleph::sim::DiffuseStepper;
using aleph::sim::ImplicitDiffuseStepper;
using aleph::sim::ImplicitError;
using aleph::sim::ImplicitWaveStepper;
using aleph::sim::Section;
using aleph::sim::ShiftedLaplacian;
using aleph::sim::StepError;
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
// Gershgorin bound on λ_max(Δ): g = max_i Σ_j |Δ_ij|.
f64 gershgorin(const DMatrix& d) {
    f64 g = 0;
    for (std::size_t i = 0; i < d.rows(); ++i) {
        f64 row = 0;
        for (std::size_t j = 0; j < d.cols(); ++j) row += std::fabs(d.at(i, j));
        if (row > g) g = row;
    }
    return g;
}
}  // namespace

// A uniform field is a fixed point of the implicit heat step (Δu = 0 so
// (I+βΔ)u = u; the SPD solve reproduces u).
TEST_CASE("implicit: diffuse uniform field is a fixed point") {
    Path p = make_path(6);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    auto st = ImplicitDiffuseStepper::make(lap.matrix, DiffuseParams{1.0}, 0.5);
    REQUIRE(st.has_value());

    Section<f64> u = Section<f64>::zeros(lap.node_order);
    for (auto& x : u.data) x = 3.25;

    REQUIRE(st->step(u).has_value());
    for (const f64 x : u.data) CHECK(std::fabs(x - 3.25) < 1e-12);
}

// THE PAYOFF: dt two orders of magnitude beyond the explicit CFL bound.
// The explicit stepper blows up (NonFinite or unbounded growth); the implicit
// step stays finite, conserves mass, and strictly smooths.
TEST_CASE("implicit: diffuse stable far beyond the explicit CFL") {
    Path p = make_path(8);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    const f64 lam  = gershgorin(lap.matrix);
    const f64 dt   = 100.0 * 2.0 / lam;   // explicit stability needs dt·α·λ_max < 2
    const DiffuseParams params{1.0};

    Section<f64> u0 = Section<f64>::zeros(lap.node_order);
    u0.data[0] = 1.0;   // a spike to smooth

    // Explicit at this dt diverges: either a NonFinite error or norm blow-up.
    {
        Section<f64> u = u0;
        const DiffuseStepper expl{params};
        bool diverged = false;
        for (int s = 0; s < 50; ++s) {
            auto r = expl.step(u, lap.matrix, dt);
            if (!r.has_value()) {
                CHECK(r.error() == StepError::NonFinite);
                diverged = true;
                break;
            }
            if (max_abs(u) > 1e6) { diverged = true; break; }
        }
        CHECK(diverged);
    }

    // Implicit at the same dt: finite, mass-conserving, variance-decreasing.
    {
        auto st = ImplicitDiffuseStepper::make(lap.matrix, params, dt);
        REQUIRE(st.has_value());
        Section<f64> u = u0;
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
        // 50 huge steps smooth the spike essentially to the mean.
        CHECK(variance_of(u) < 1e-6);
    }
}

// For a small dt the implicit and explicit steps agree to O(dt²) per entry
// (both are first-order discretizations of the same flow).
TEST_CASE("implicit: diffuse small-dt consistency with explicit") {
    Path p = make_path(6);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);
    const f64 dt = 1e-3;
    const DiffuseParams params{1.0};

    Section<f64> ue = Section<f64>::zeros(lap.node_order);
    ue.data[2] = 1.0;
    Section<f64> ui = ue;

    REQUIRE(DiffuseStepper{params}.step(ue, lap.matrix, dt).has_value());
    auto st = ImplicitDiffuseStepper::make(lap.matrix, params, dt);
    REQUIRE(st.has_value());
    REQUIRE(st->step(ui).has_value());

    for (std::size_t i = 0; i < ue.size(); ++i)
        CHECK(std::fabs(ue.data[i] - ui.data[i]) < 10.0 * dt * dt);
}

// Implicit wave: a kick then many steps at a dt far beyond the explicit CFL —
// everything stays finite and bounded (backward Euler only removes energy).
TEST_CASE("implicit: wave stable and bounded at huge dt") {
    Path p = make_path(8);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    const WaveParams params{};   // c = 1, damping = 0.999
    const f64 lam = gershgorin(lap.matrix);
    const f64 dt  = 10.0 * 2.0 / std::sqrt(lam);   // c²dt²λ_max ≫ 4

    CHECK_FALSE(aleph::sim::WaveStepper::cfl_ok(lap.matrix, params, dt));

    auto st = ImplicitWaveStepper::make(lap.matrix, params, dt);
    REQUIRE(st.has_value());

    Section<f64> u = Section<f64>::zeros(lap.node_order);
    Section<f64> v = Section<f64>::zeros(lap.node_order);
    // Zero-net-momentum kick: a net kick would (correctly) drift the free
    // translation mode (Δ's kernel) — physics, not instability. The stability
    // claim is about the oscillatory part, so keep the mean pinned at 0.
    v.data[3] = 1.5;
    v.data[4] = -1.5;

    f64 peak = 0.0;
    for (int s = 0; s < 200; ++s) {
        REQUIRE(st->step(u, v).has_value());
        peak = std::max(peak, std::max(max_abs(u), max_abs(v)));
    }
    CHECK(std::isfinite(peak));
    CHECK(peak < 10.0);   // bounded — no blow-up at 10x the CFL limit
    // Backward Euler only removes energy: by 200 huge steps the field is flat.
    CHECK(variance_of(u) < 1e-6);
    CHECK(variance_of(v) < 1e-6);
}

// A uniform displacement with zero velocity is a fixed point of the implicit
// wave step: Δu = 0 ⇒ u⁺ = u and v⁺ = 0.
TEST_CASE("implicit: wave uniform zero-velocity fixed point") {
    Path p = make_path(5);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    auto st = ImplicitWaveStepper::make(lap.matrix, WaveParams{}, 0.25);
    REQUIRE(st.has_value());

    Section<f64> u = Section<f64>::zeros(lap.node_order);
    Section<f64> v = Section<f64>::zeros(lap.node_order);
    for (auto& x : u.data) x = -1.75;

    REQUIRE(st->step(u, v).has_value());
    for (const f64 x : u.data) CHECK(std::fabs(x + 1.75) < 1e-12);
    for (const f64 x : v.data) CHECK(std::fabs(x) < 1e-12);
}

// Same (Δ, params, dt, kick) ⇒ byte-identical trajectories run-to-run.
TEST_CASE("implicit: deterministic byte-identical trajectories") {
    Path p = make_path(7);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    auto run = [&](Section<f64>& u_out, Section<f64>& v_out) {
        auto st = ImplicitWaveStepper::make(lap.matrix, WaveParams{}, 0.7);
        REQUIRE(st.has_value());
        Section<f64> u = Section<f64>::zeros(lap.node_order);
        Section<f64> v = Section<f64>::zeros(lap.node_order);
        v.data[2] = 2.0;
        for (int s = 0; s < 32; ++s) REQUIRE(st->step(u, v).has_value());
        u_out = u; v_out = v;
    };
    Section<f64> u1, v1, u2, v2;
    run(u1, v1);
    run(u2, v2);
    for (std::size_t i = 0; i < u1.size(); ++i) {
        CHECK(u1.data[i] == u2.data[i]);
        CHECK(v1.data[i] == v2.data[i]);
    }
}

// Error contract: bad make inputs and failed steps leave sections unchanged.
TEST_CASE("implicit: make/step errors and untouched-on-error") {
    Path p = make_path(4);
    const auto lap = aleph::flow::build_laplacian_bounded(p.g, aleph::flow::default_weight);

    // dt <= 0 -> InvalidShift.
    {
        auto st = ImplicitDiffuseStepper::make(lap.matrix, DiffuseParams{}, 0.0);
        REQUIRE_FALSE(st.has_value());
        CHECK(st.error() == ImplicitError::InvalidShift);
    }
    // Non-symmetric delta -> FactorFailed.
    {
        DMatrix bad = DMatrix::zeros(3, 3);
        bad.at(0, 1) = 1.0;   // asymmetric
        auto st = ShiftedLaplacian::make(bad, 1.0);
        REQUIRE_FALSE(st.has_value());
        CHECK(st.error() == ImplicitError::FactorFailed);
    }
    // Wrong-size section -> DimMismatch, section untouched.
    {
        auto st = ImplicitDiffuseStepper::make(lap.matrix, DiffuseParams{}, 0.5);
        REQUIRE(st.has_value());
        Section<f64> u = Section<f64>::zeros({p.ids[0], p.ids[1]});  // size 2 != 4
        u.data[0] = 42.0;
        auto r = st->step(u);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == StepError::DimMismatch);
        CHECK(u.data[0] == 42.0);
    }
    // A seeded inf -> NonFinite, section untouched (stronger than explicit).
    {
        auto st = ImplicitDiffuseStepper::make(lap.matrix, DiffuseParams{}, 0.5);
        REQUIRE(st.has_value());
        Section<f64> u = Section<f64>::zeros(lap.node_order);
        u.data[1] = std::numeric_limits<f64>::infinity();
        auto r = st->step(u);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == StepError::NonFinite);
        CHECK(std::isinf(u.data[1]));
        CHECK(u.data[0] == 0.0);
    }
}
