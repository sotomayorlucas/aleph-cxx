module;
// Weighted graph Laplacian  Delta = D_w - A_w  from Ollivier-Ricci curvature.
//
// For every Adjacent edge (a, b) of the 1-skeleton with curvature kappa, the
// edge weight is w = weight_fn(kappa). The symmetric weighted Laplacian is the
// dense matrix
//     Delta(a, a) += w   Delta(b, b) += w
//     Delta(a, b) -= w   Delta(b, a) -= w
// accumulated over all edges. With non-negative weights Delta is symmetric
// PSD, the constant vector lies in its kernel (every row sums to 0), and the
// node order is stable (the sorted OneSkeleton vertices).
//
// Ported faithfully from aleph-engine/aleph-flow/src/laplacian.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * The Rust `WeightedLaplacian` derives Clone; `curvatures` here is an
//     OrderedMap (move-only), so this struct is move-only. Callers construct
//     it via the free builder functions and move it; no copy is needed.
//   * Rust `DMatrix::{get,set}` map to the C++ `DMatrix::at(i,j)` accessor.
//   * The weight function is a plain function pointer (f64(*)(f64)), not a
//     std::function, to stay aleph_flags_isa-clean.
//   * default_weight matches the Rust reference exactly: it clamps
//     kappa >= -0.95 (so weights stay >= 0.05, the M11 defense-in-depth) and
//     returns 1.0 + clamped. (The 4d plan summary names exp(-kappa); the named
//     Rust source is the ground truth and uses 1.0 + clamped, so we port that.)
//
// Determinism (spec 7.5): all f64, no parallelism, fixed column-major order,
// stable node_order derived from the (sorted) OneSkeleton vertices.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

export module aleph.flow:laplacian;

import :ollivier_ricci;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.containers;
import aleph.linalg.sparse;
import aleph.math;

export namespace aleph::flow {

using aleph::math::f64;
using aleph::types::NodeId;
using aleph::sheaf::OneSkeleton;
using aleph::linalg::sparse::DMatrix;

// Edge-weight function pointer: maps an Ollivier-Ricci curvature to a weight.
using WeightFn = f64 (*)(f64);

// Edge weight from Ollivier-Ricci curvature (port default_weight).
//
// Clamps kappa >= -0.95 so weights stay >= 0.05. This is a defense-in-depth
// measure against degenerate Wasserstein-1 outputs that could otherwise
// zero-weight an edge and silently disconnect the Laplacian (M11 finding).
[[nodiscard]] inline f64 default_weight(f64 kappa) {
    const f64 clamped = std::max(kappa, -0.95);
    return 1.0 + clamped;
}

// Weighted graph Laplacian Delta = D_w - A_w.
//
// Move-only: `curvatures` is an OrderedMap (move-only). node_order is the
// stable sorted vertex list; matrix is the n x n dense Laplacian; curvatures
// is the per-edge curvature map (sorted node-id pair -> kappa).
struct WeightedLaplacian {
    std::vector<NodeId> node_order;
    DMatrix             matrix;
    RicciMap            curvatures;

    // True when the matrix equals its own transpose within eps
    // (port WeightedLaplacian::is_symmetric).
    [[nodiscard]] bool is_symmetric(f64 eps) const {
        const DMatrix t = matrix.transpose();
        return matrix.approx_eq(t, eps);
    }

    // True when the all-ones vector lies (within eps) in the kernel of the
    // Laplacian, i.e. every row sums to 0 (port WeightedLaplacian::ones_in_kernel).
    [[nodiscard]] bool ones_in_kernel(f64 eps) const {
        const std::size_t n = node_order.size();
        if (n == 0) {
            return true;
        }
        const std::vector<f64> ones(n, 1.0);
        const std::vector<f64> out = matrix.matvec(std::span<const f64>(ones));
        for (const f64 x : out) {
            if (std::abs(x) >= eps) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace aleph::flow

namespace aleph::flow::detail {

using aleph::math::f64;
using aleph::types::NodeId;
using aleph::sheaf::OneSkeleton;
using aleph::linalg::sparse::DMatrix;

// Shared assembly: build the dense Delta = D_w - A_w from a precomputed
// curvature map over the skeleton's stable vertex order. Mirrors the body of
// build_laplacian / build_laplacian_w2 (which differ only in how `curvatures`
// is produced).
inline WeightedLaplacian assemble(const OneSkeleton& skel, RicciMap curvatures,
                                  WeightFn weight_fn) {
    std::vector<NodeId> node_order(skel.vertices.begin(), skel.vertices.end());
    const std::size_t   n = node_order.size();

    // node -> index (stable, by skeleton vertex order).
    aleph::containers::OrderedMap<NodeId, std::size_t> node_to_idx;
    for (std::size_t i = 0; i < n; ++i) {
        node_to_idx.insert(node_order[i], i);
    }

    DMatrix matrix = DMatrix::zeros(n, n);
    for (const auto& [edge, kappa] : curvatures) {
        const std::size_t* ia = node_to_idx.get(edge.first);
        const std::size_t* ib = node_to_idx.get(edge.second);
        if (ia == nullptr || ib == nullptr) {
            continue;
        }
        const f64         w  = weight_fn(kappa);
        const std::size_t a  = *ia;
        const std::size_t b  = *ib;
        matrix.at(a, b) -= w;
        matrix.at(b, a) -= w;
        matrix.at(a, a) += w;
        matrix.at(b, b) += w;
    }
    return WeightedLaplacian{std::move(node_order), std::move(matrix),
                             std::move(curvatures)};
}

}  // namespace aleph::flow::detail

export namespace aleph::flow {

// Build the weighted Laplacian Delta = D_w - A_w from a graph using the
// Ollivier-Ricci W_1 curvature (port build_laplacian).
[[nodiscard]] inline WeightedLaplacian build_laplacian(
    const aleph::graph::Graph& g, WeightFn weight_fn) {
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    RicciMap          curvatures = ricci_curvature_from_skeleton(skel);
    return detail::assemble(skel, std::move(curvatures), weight_fn);
}

// Same as build_laplacian but uses the smooth W_2^eps curvature
// (ricci_curvature_w2) instead of the W_1 simplex (port build_laplacian_w2).
// `epsilon` is the entropic regularisation strength (typical: 0.05).
[[nodiscard]] inline WeightedLaplacian build_laplacian_w2(
    const aleph::graph::Graph& g, WeightFn weight_fn, f64 epsilon) {
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    RicciMap          curvatures = ricci_curvature_w2(skel, epsilon);
    return detail::assemble(skel, std::move(curvatures), weight_fn);
}

}  // namespace aleph::flow
