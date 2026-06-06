#include "doctest.h"
#include <cmath>
#include <limits>
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
using aleph::sim::VectorDiffuseStepper;
using aleph::sim::VectorDiffuseParams;
using aleph::sim::StepError;
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
// Per-component sum of a Vec3 section.
f64 sum_x(const Section<Vec3>& u) { f64 s = 0; for (const Vec3& v : u.data) s += static_cast<f64>(v.x); return s; }
f64 sum_y(const Section<Vec3>& u) { f64 s = 0; for (const Vec3& v : u.data) s += static_cast<f64>(v.y); return s; }
f64 sum_z(const Section<Vec3>& u) { f64 s = 0; for (const Vec3& v : u.data) s += static_cast<f64>(v.z); return s; }
}  // namespace

TEST_CASE("VectorDiffuseStepper: component independence == scalar physics (one step)") {
    Path p = make_path(5);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
    REQUIRE(L.node_order.size() == 5);

    // x-spike Vec3 field (one node x=1, y=z=0) and the same scalar spike.
    Section<Vec3> uv = Section<Vec3>::zeros(L.node_order);
    uv.data[0] = Vec3{1.0f, 0.0f, 0.0f};
    Section<f64> us = Section<f64>::zeros(L.node_order);
    us.data = {1.0, 0.0, 0.0, 0.0, 0.0};

    VectorDiffuseStepper vs{VectorDiffuseParams{/*alpha=*/0.5}};
    DiffuseStepper       ss{DiffuseParams{/*alpha=*/0.5}};
    const f64 lmax = gershgorin(L.matrix);
    REQUIRE(lmax > 0.0);
    const f64 dt = 0.5 / (vs.params.alpha * lmax);   // dt·α·λmax = 0.5 < 2
    REQUIRE(dt * vs.params.alpha * lmax < 2.0);

    REQUIRE(vs.step(uv, L.matrix, dt).has_value());
    REQUIRE(ss.step(us, L.matrix, dt).has_value());

    f64 max_diff = 0.0;
    for (std::size_t i = 0; i < uv.size(); ++i) {
        const f64 d = std::fabs(static_cast<f64>(uv.data[i].x) - us.data[i]);
        if (d > max_diff) max_diff = d;
        CHECK(d < 1e-5);                 // x-component tracks the scalar field
        CHECK(uv.data[i].y == 0.0f);     // y/z untouched (component-wise, no coupling)
        CHECK(uv.data[i].z == 0.0f);
    }
    MESSAGE("component-vs-scalar max diff = " << max_diff);
}

TEST_CASE("VectorDiffuseStepper: per-component sum conservation (one step)") {
    Path p = make_path(6);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);

    Section<Vec3> u = Section<Vec3>::zeros(L.node_order);
    // An arbitrary x/y/z field.
    u.data[0] = Vec3{ 1.0f, -2.0f,  0.5f};
    u.data[1] = Vec3{-0.5f,  3.0f, -1.0f};
    u.data[2] = Vec3{ 2.0f,  0.0f,  4.0f};
    u.data[4] = Vec3{ 0.25f, 1.5f, -2.0f};

    VectorDiffuseStepper vs{VectorDiffuseParams{0.5}};
    const f64 lmax = gershgorin(L.matrix);
    const f64 dt = 0.5 / (vs.params.alpha * lmax);

    const f64 sx0 = sum_x(u), sy0 = sum_y(u), sz0 = sum_z(u);
    REQUIRE(vs.step(u, L.matrix, dt).has_value());

    const f64 tol = 1e-5 * static_cast<f64>(u.size());
    const f64 rx = std::fabs(sum_x(u) - sx0);
    const f64 ry = std::fabs(sum_y(u) - sy0);
    const f64 rz = std::fabs(sum_z(u) - sz0);
    CHECK(rx < tol);
    CHECK(ry < tol);
    CHECK(rz < tol);
    MESSAGE("sum-conservation residuals: x=" << rx << " y=" << ry << " z=" << rz << " (tol=" << tol << ")");
}

TEST_CASE("VectorDiffuseStepper: a spike smooths into its neighbour") {
    Path p = make_path(5);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);

    Section<Vec3> u = Section<Vec3>::zeros(L.node_order);
    u.data[0] = Vec3{1.0f, 2.0f, 3.0f};  // single-node spike (all components)

    VectorDiffuseStepper vs{VectorDiffuseParams{0.5}};
    const f64 lmax = gershgorin(L.matrix);
    const f64 dt = 0.5 / (vs.params.alpha * lmax);

    REQUIRE(vs.step(u, L.matrix, dt).has_value());

    // The neighbour gained from 0 on every component.
    CHECK(u.data[1].x > 0.0f);
    CHECK(u.data[1].y > 0.0f);
    CHECK(u.data[1].z > 0.0f);
    // The spike shrank.
    CHECK(u.data[0].x < 1.0f);
    CHECK(u.data[0].y < 2.0f);
    CHECK(u.data[0].z < 3.0f);
}

TEST_CASE("VectorDiffuseStepper: empty field and dim mismatch report errors, no throw") {
    VectorDiffuseStepper vs{};
    Section<Vec3> empty = Section<Vec3>::zeros(std::vector<NodeId>{});
    auto r1 = vs.step(empty, aleph::linalg::sparse::DMatrix::zeros(0, 0), 0.01);
    CHECK(!r1.has_value());
    CHECK(r1.error() == StepError::EmptyField);

    Section<Vec3> two = Section<Vec3>::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    auto r2 = vs.step(two, aleph::linalg::sparse::DMatrix::zeros(3, 3), 0.01);
    CHECK(!r2.has_value());
    CHECK(r2.error() == StepError::DimMismatch);
}

TEST_CASE("VectorDiffuseStepper: a seeded non-finite input reports NonFinite") {
    Path p = make_path(4);
    auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);

    Section<Vec3> u = Section<Vec3>::zeros(L.node_order);
    u.data[1].x = std::numeric_limits<float>::infinity();  // seed an inf

    VectorDiffuseStepper vs{VectorDiffuseParams{0.5}};
    const f64 lmax = gershgorin(L.matrix);
    const f64 dt = 0.5 / (vs.params.alpha * lmax);

    auto r = vs.step(u, L.matrix, dt);
    CHECK(!r.has_value());
    CHECK(r.error() == StepError::NonFinite);
}

TEST_CASE("VectorDiffuseStepper: deterministic — identical state across two runs") {
    auto run = [] {
        Path p = make_path(6);
        auto L = aleph::flow::build_laplacian(p.g, aleph::flow::default_weight);
        Section<Vec3> u = Section<Vec3>::zeros(L.node_order);
        u.data[0] = Vec3{1.0f, -1.0f, 0.5f};
        u.data[3] = Vec3{-1.0f, 2.0f, -0.5f};
        VectorDiffuseStepper vs{VectorDiffuseParams{0.5}};
        const f64 lmax = gershgorin(L.matrix);
        const f64 dt = 0.5 / (vs.params.alpha * lmax);
        (void)vs.step(u, L.matrix, dt);
        (void)vs.step(u, L.matrix, dt);
        return u.data;
    };
    std::vector<Vec3> a = run(), b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i] == b[i]);   // Vec3::operator== compares x,y,z
}
