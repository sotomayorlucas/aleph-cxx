// Sparse assembly of the weighted Laplacian (spec 2026-07-04).
//
// The dense assembly allocates and zeroes an n x n DMatrix per rebuild — the
// O(n^2) knee in the paper's evaluation. assemble_sparse produces a CsrMatrix
// whose VALUES are bit-identical to the dense assembly (same per-node
// diagonal accumulation order; off-diagonals are single writes), and whose
// matvec is bit-identical for finite fields (adding 0.0*x[j] is an exact
// no-op and both traversals visit columns in ascending order). These tests
// pin all of that bitwise.

#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

import aleph.flow;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.sim;
import aleph.math;
import aleph.linalg.sparse;

using aleph::flow::build_laplacian_bounded;
using aleph::flow::build_laplacian_bounded_sparse;
using aleph::flow::build_laplacian_local_sparse;
using aleph::flow::default_weight;
using aleph::flow::SparseWeightedLaplacian;
using aleph::flow::two_hop_touched_edges;
using aleph::flow::WeightedLaplacian;
using aleph::graph::Graph;
using aleph::math::f64;
using aleph::sheaf::OneSkeleton;
using aleph::types::EdgeKind;
using aleph::types::Mesh;
using aleph::types::NodeId;

namespace {

struct Grid {
    Graph                            g;
    std::vector<std::vector<NodeId>> ids;
};

Grid make_grid(std::size_t R) {
    Grid grid;
    grid.ids.assign(R, std::vector<NodeId>(R));
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            const NodeId id = grid.g.alloc_node_id();
            grid.g.insert_node(Mesh{
                id,
                std::string("m") + std::to_string(i) + "_" + std::to_string(j),
                1});
            grid.ids[i][j] = id;
        }
    }
    for (std::size_t i = 0; i < R; ++i) {
        for (std::size_t j = 0; j < R; ++j) {
            if (j + 1 < R)
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i][j + 1])
                            .has_value());
            if (i + 1 < R)
                REQUIRE(grid.g
                            .add_edge(EdgeKind::Adjacent, grid.ids[i][j],
                                      grid.ids[i + 1][j])
                            .has_value());
        }
    }
    return grid;
}

NodeId add_object(Graph& g, const std::vector<NodeId>& to) {
    const NodeId c = g.alloc_node_id();
    g.insert_node(Mesh{c, std::string("add") + std::to_string(c.value), 1});
    for (const NodeId t : to) {
        REQUIRE(g.add_edge(EdgeKind::Adjacent, c, t).has_value());
    }
    return c;
}

// Every entry of the sparse matrix equals the dense one BITWISE, plus the
// structural invariants (diagonal always stored -> nnz == n + 2|E|).
void assert_sparse_eq_dense(const SparseWeightedLaplacian& sp,
                            const WeightedLaplacian&       de,
                            std::size_t                    n_edges) {
    REQUIRE(sp.node_order == de.node_order);
    const std::size_t n = de.node_order.size();
    REQUIRE(sp.matrix.rows() == n);
    REQUIRE(sp.matrix.cols() == n);
    CHECK(sp.matrix.nnz() == n + 2 * n_edges);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            CHECK(sp.matrix.get(i, j) == de.matrix.at(i, j));
    REQUIRE(sp.curvatures.size() == de.curvatures.size());
    for (const auto& [e, kd] : de.curvatures) {
        const f64* ks = sp.curvatures.get(e);
        REQUIRE(ks != nullptr);
        CHECK(*ks == kd);
    }
}

}  // namespace

TEST_CASE("sparse-lap: sparse build == dense build, bitwise, every entry") {
    Grid grid = make_grid(6);
    const NodeId c = add_object(grid.g, {grid.ids[2][2]});
    (void)c;

    const WeightedLaplacian de =
        build_laplacian_bounded(grid.g, default_weight);
    const SparseWeightedLaplacian sp =
        build_laplacian_bounded_sparse(grid.g, default_weight);

    const OneSkeleton skel = OneSkeleton::from_graph(grid.g);
    assert_sparse_eq_dense(sp, de, skel.edges.size());
    CHECK(sp.is_symmetric(1e-12));
    CHECK(sp.ones_in_kernel(1e-12));
}

TEST_CASE("sparse-lap: localized sparse == full sparse, bitwise, edit trace") {
    Grid grid = make_grid(6);
    SparseWeightedLaplacian prev =
        build_laplacian_bounded_sparse(grid.g, default_weight);

    // add -> add(2-edge) -> delete, threading prev; every step compared to a
    // fresh full sparse build AND to the dense build.
    const NodeId c1 = add_object(grid.g, {grid.ids[2][2]});
    NodeId       c2{};
    for (int step = 0; step < 3; ++step) {
        std::vector<NodeId> seed;
        if (step == 0) {
            seed = {c1, grid.ids[2][2]};
        } else if (step == 1) {
            c2   = add_object(grid.g, {grid.ids[3][3], grid.ids[3][4]});
            seed = {c2, grid.ids[3][3], grid.ids[3][4]};
        } else {
            grid.g.remove_node_cascade(c2);
            seed = {grid.ids[3][3], grid.ids[3][4]};
        }
        const OneSkeleton skel  = OneSkeleton::from_graph(grid.g);
        const auto        dirty = two_hop_touched_edges(skel, seed);

        int rc = 0;
        const SparseWeightedLaplacian local = build_laplacian_local_sparse(
            grid.g, prev, dirty, default_weight, &rc);
        SparseWeightedLaplacian full =
            build_laplacian_bounded_sparse(grid.g, default_weight);
        const WeightedLaplacian de =
            build_laplacian_bounded(grid.g, default_weight);

        CHECK(rc == static_cast<int>(dirty.size()));
        assert_sparse_eq_dense(local, de, skel.edges.size());
        assert_sparse_eq_dense(full, de, skel.edges.size());
        prev = std::move(full);
    }
}

// matvec across carriers: value-equivalent to a few ulps (NOT bitwise — ISO
// FP contraction may pick fma-vs-mul+add differently per loop shape; spec
// 2026-07-04 §1), and the sparse matvec is byte-deterministic run-to-run.
TEST_CASE("sparse-lap: matvec ulp-close to dense, byte-deterministic itself") {
    Grid grid = make_grid(5);
    const WeightedLaplacian de =
        build_laplacian_bounded(grid.g, default_weight);
    const SparseWeightedLaplacian sp =
        build_laplacian_bounded_sparse(grid.g, default_weight);

    const std::size_t n = de.node_order.size();
    std::vector<f64>  x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = 0.25 * static_cast<f64>(i) - 3.0;  // non-trivial finite field

    const std::vector<f64> yd = de.matrix.matvec(std::span<const f64>(x));
    const std::vector<f64> ys = sp.matrix.matvec(std::span<const f64>(x));
    REQUIRE(yd.size() == ys.size());
    for (std::size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        const f64 scale = std::max(1.0, std::fabs(yd[i]));
        CHECK(std::fabs(yd[i] - ys[i]) <= 1e-14 * scale);
    }

    const std::vector<f64> ys2 = sp.matrix.matvec(std::span<const f64>(x));
    for (std::size_t i = 0; i < n; ++i) CHECK(ys[i] == ys2[i]);
}

// Stepper trajectories on the sparse carrier: value-equivalent to dense
// (tight tolerance — ulp-level matvec differences compound over steps) and
// byte-deterministic against a repeat run of themselves.
TEST_CASE("sparse-lap: wave/diffuse trajectories equivalent + reproducible") {
    Grid grid = make_grid(5);
    const WeightedLaplacian de =
        build_laplacian_bounded(grid.g, default_weight);
    const SparseWeightedLaplacian sp =
        build_laplacian_bounded_sparse(grid.g, default_weight);

    using aleph::sim::DiffuseParams;
    using aleph::sim::DiffuseStepper;
    using aleph::sim::Section;
    using aleph::sim::WaveParams;
    using aleph::sim::WaveStepper;

    const f64 dt = 0.05;

    // Wave: kick, 32 steps on each carrier; sparse repeated for bitwise self-
    // reproducibility.
    {
        const WaveStepper st{WaveParams{}};
        auto run = [&](const auto& op, Section<f64>& u, Section<f64>& v) {
            u = Section<f64>::zeros(de.node_order);
            v = Section<f64>::zeros(de.node_order);
            v.data[3] = 1.0;
            for (int s = 0; s < 32; ++s)
                REQUIRE(st.step(u, v, op, dt).has_value());
        };
        Section<f64> ud, vd, us, vs, us2, vs2;
        run(de.matrix, ud, vd);
        run(sp.matrix, us, vs);
        run(sp.matrix, us2, vs2);
        for (std::size_t i = 0; i < ud.size(); ++i) {
            CAPTURE(i);
            CHECK(std::fabs(ud.data[i] - us.data[i]) <= 1e-12);
            CHECK(std::fabs(vd.data[i] - vs.data[i]) <= 1e-12);
            CHECK(us.data[i] == us2.data[i]);   // bitwise self-reproducible
            CHECK(vs.data[i] == vs2.data[i]);
        }
    }
    // Diffuse: spike, 32 steps on each carrier.
    {
        const DiffuseStepper st{DiffuseParams{}};
        auto run = [&](const auto& op, Section<f64>& u) {
            u = Section<f64>::zeros(de.node_order);
            u.data[7] = 2.0;
            for (int s = 0; s < 32; ++s)
                REQUIRE(st.step(u, op, dt).has_value());
        };
        Section<f64> ud, us, us2;
        run(de.matrix, ud);
        run(sp.matrix, us);
        run(sp.matrix, us2);
        for (std::size_t i = 0; i < ud.size(); ++i) {
            CAPTURE(i);
            CHECK(std::fabs(ud.data[i] - us.data[i]) <= 1e-12);
            CHECK(us.data[i] == us2.data[i]);
        }
    }

    // CFL guard agrees across carriers (Gershgorin bounds ulp-close; dt far
    // from the threshold).
    CHECK(aleph::sim::WaveStepper::cfl_ok(de.matrix, WaveParams{}, dt)
          == aleph::sim::WaveStepper::cfl_ok(sp.matrix, WaveParams{}, dt));
}
