module;
// High-level state tying Ricci + Laplacian + LDL^T together, with incremental
// refactorization on DPO rewrites.
//
// Ported faithfully from aleph-engine/aleph-flow/src/flow.rs.
//
// IncrementalLaplacian caches the weighted graph Laplacian Delta and an LDL^T
// factorization of it. The factor is DENSE (the rank-k-updatable LDLT) when
// n < SPARSE_THRESHOLD, else SPARSE (SparseLdlt, which always refactors from
// scratch on a rewrite — sparse rank-k update is deferred). solve(b) solves
// Delta x = b on the current factor; with the all-ones vector in the kernel of
// the Laplacian this is the building block of the implicit-Euler heat step
// (I + dt*Delta) phi_{n+1} = phi_n.
//
// apply_rewrite mirrors the Rust control flow exactly:
//   * n >= SPARSE_THRESHOLD          -> refactor from scratch (sparse).
//   * node set changed               -> refactor from scratch (dense).
//   * node set stable (dense path)   -> jacobi_eigh of the Laplacian delta,
//                                       collect the non-trivial eigenpairs as a
//                                       rank-k update (|lambda| > 1e-12), try the
//                                       in-place LDLT rank_k_update; on failure,
//                                       refactor from scratch.
//
// jacobi_eigh is ported verbatim, including the tie-breaking rule: the pivot
// (p, q) is the FIRST-FOUND largest off-diagonal in the i<j scan order (a strict
// `> off_max` comparison never overwrites on ties), so eigenvectors are
// bit-identical to the Rust reference.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust `enum LaplacianFactor { Dense(LDLT), Sparse(SparseLdlt) }` maps to
//     std::variant<LDLT, SparseLdlt>. Both alternatives are copyable.
//   * Rust returns `Result<_, LdltError>`; C++ uses std::expected with the
//     LdltErrorInfo payload. solve returns std::optional (kernel-aware; None
//     when b is not in the range of Delta), per aleph_flags_isa (no exceptions).
//   * WeightedLaplacian holds a move-only RicciMap (OrderedMap), so its
//     curvatures cannot be copied; IncrementalLaplacian is therefore move-only.
//     The Rust struct derives Clone, but the C++ flow never clones it (the
//     proptest builds a fresh one for the comparison), so move-only is faithful.
//   * Rust's `DMatrix::{get,set}` map to the C++ `DMatrix::at(i,j)` accessor.
//   * apply_rewrite keeps the Rust signature (g_before, g_after, preserved);
//     g_before and preserved are unused in the body there too (the rewrite only
//     reads g_after), so they are accepted and ignored here as well.
//   * `unreachable!()` (n < SPARSE_THRESHOLD but factor is Sparse) maps to a
//     defensive refactor-from-scratch rather than a trap.
//
// Determinism (spec 7.5): all f64, no parallelism, fixed column-major order,
// stable node_order, first-found Jacobi pivot.
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

export module aleph.flow:flow;

import :laplacian;
import :ollivier_ricci;
import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.sparse;
import aleph.math;

namespace aleph::flow::detail {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;

// Symmetric eigendecomposition via cyclic Jacobi rotations (port jacobi_eigh).
// Returns (eigenvalues, eigenvector_matrix) where the i-th column of the matrix
// is the eigenvector for the i-th eigenvalue.
//
// The pivot search uses a strict `aij > off_max` comparison, so on ties the
// FIRST-FOUND (smallest (p, q) in the i<j scan) wins — this tie-breaking is
// load-bearing for bit-identical eigenvectors across the Rust/C++ ports.
inline std::pair<std::vector<f64>, DMatrix> jacobi_eigh(const DMatrix& m) {
    const std::size_t n = m.rows();
    DMatrix a = m;
    DMatrix v = DMatrix::identity(n);
    const std::size_t nm = (n > 1) ? n : 1;
    const std::size_t max_iter = 50 * nm * nm;
    for (std::size_t iter = 0; iter < max_iter; ++iter) {
        // Find largest off-diagonal in absolute value (first-found on ties).
        std::size_t p = 0;
        std::size_t q = 1;
        f64 off_max = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                const f64 aij = std::abs(a.at(i, j));
                if (aij > off_max) {
                    off_max = aij;
                    p = i;
                    q = j;
                }
            }
        }
        if (off_max < 1e-14) {
            break;
        }
        const f64 app = a.at(p, p);
        const f64 aqq = a.at(q, q);
        const f64 apq = a.at(p, q);
        const f64 theta = (aqq - app) / (2.0 * apq);
        const f64 t = (theta >= 0.0)
                          ? 1.0 / (theta + std::sqrt(1.0 + theta * theta))
                          : 1.0 / (theta - std::sqrt(1.0 + theta * theta));
        const f64 c = 1.0 / std::sqrt(1.0 + t * t);
        const f64 s = t * c;
        for (std::size_t i = 0; i < n; ++i) {
            if (i != p && i != q) {
                const f64 aip = a.at(i, p);
                const f64 aiq = a.at(i, q);
                a.at(i, p) = c * aip - s * aiq;
                a.at(p, i) = c * aip - s * aiq;
                a.at(i, q) = s * aip + c * aiq;
                a.at(q, i) = s * aip + c * aiq;
            }
        }
        a.at(p, p) = app - t * apq;
        a.at(q, q) = aqq + t * apq;
        a.at(p, q) = 0.0;
        a.at(q, p) = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const f64 vip = v.at(i, p);
            const f64 viq = v.at(i, q);
            v.at(i, p) = c * vip - s * viq;
            v.at(i, q) = s * vip + c * viq;
        }
    }
    std::vector<f64> eigvals(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        eigvals[i] = a.at(i, i);
    }
    return {std::move(eigvals), std::move(v)};
}

}  // namespace aleph::flow::detail

export namespace aleph::flow {

using aleph::math::f64;
using aleph::types::NodeId;
using aleph::linalg::sparse::DMatrix;
using aleph::linalg::sparse::CsrMatrix;
using aleph::linalg::sparse::LDLT;
using aleph::linalg::sparse::SparseLdlt;
using aleph::linalg::sparse::LdltErrorInfo;

// Dense vs sparse cutoff. Below this n, use the original dense LDL^T + rank-k
// update path (preserves the incremental tests). At or above, use sparse LDL^T
// (always refactors from scratch on apply_rewrite). Copied verbatim from Rust.
inline constexpr std::size_t SPARSE_THRESHOLD = 32;

// LaplacianFactor = Dense(LDLT) | Sparse(SparseLdlt) (port the Rust enum).
// Index 0 == Dense, index 1 == Sparse.
using LaplacianFactor = std::variant<LDLT, SparseLdlt>;

// Symmetric eigendecomposition via cyclic Jacobi rotations. Returns
// (eigenvalues, eigenvector_matrix); the i-th column of the matrix is the
// eigenvector for the i-th eigenvalue. This is the rank-k update's eigenpair
// source. In the Rust module jacobi_eigh is a private fn tested by the module's
// own #[cfg(test)] (jacobi_eigh_2x2_matches_known); it is exported here so that
// in-module oracle ports verbatim. Tie-breaking is first-found p<q (see detail).
[[nodiscard]] inline std::pair<std::vector<f64>, DMatrix>
jacobi_eigh(const DMatrix& m) {
    return detail::jacobi_eigh(m);
}

// Cached weighted graph Laplacian + its LDL^T factorization, refactorized
// incrementally on DPO rewrites (port IncrementalLaplacian).
//
// Move-only: `curvatures` is a move-only RicciMap (OrderedMap). The struct is
// constructed via from_graph / from_graph_w2 and mutated in place; it is never
// copied (matching how the Rust flow uses it).
struct IncrementalLaplacian {
    std::vector<NodeId> node_order;
    DMatrix             laplacian;
    RicciMap            curvatures;
    LaplacianFactor     factor;

    // Build from scratch from a graph (port IncrementalLaplacian::from_graph).
    // O(n^3) for the dense path, O(n * nnz) for the sparse path.
    [[nodiscard]] static std::expected<IncrementalLaplacian, LdltErrorInfo>
    from_graph(const aleph::graph::Graph& g) {
        WeightedLaplacian l = build_laplacian(g, default_weight);
        const std::size_t n = l.node_order.size();
        LaplacianFactor   factor;
        if (n < SPARSE_THRESHOLD) {
            auto f = LDLT::factorize(l.matrix);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor = LaplacianFactor{std::in_place_index<0>, std::move(*f)};
        } else {
            const CsrMatrix csr = CsrMatrix::from_dense(l.matrix);
            auto f = SparseLdlt::factorize(csr);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor = LaplacianFactor{std::in_place_index<1>, std::move(*f)};
        }
        return IncrementalLaplacian{std::move(l.node_order), std::move(l.matrix),
                                    std::move(l.curvatures), std::move(factor)};
    }

    // Same as from_graph but uses the smooth W_2^eps curvature
    // (port IncrementalLaplacian::from_graph_w2). `epsilon` is the entropic
    // regularisation strength.
    [[nodiscard]] static std::expected<IncrementalLaplacian, LdltErrorInfo>
    from_graph_w2(const aleph::graph::Graph& g, f64 epsilon) {
        WeightedLaplacian l = build_laplacian_w2(g, default_weight, epsilon);
        const std::size_t n = l.node_order.size();
        LaplacianFactor   factor;
        if (n < SPARSE_THRESHOLD) {
            auto f = LDLT::factorize(l.matrix);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor = LaplacianFactor{std::in_place_index<0>, std::move(*f)};
        } else {
            const CsrMatrix csr = CsrMatrix::from_dense(l.matrix);
            auto f = SparseLdlt::factorize(csr);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor = LaplacianFactor{std::in_place_index<1>, std::move(*f)};
        }
        return IncrementalLaplacian{std::move(l.node_order), std::move(l.matrix),
                                    std::move(l.curvatures), std::move(factor)};
    }

    // Apply a DPO rewrite (port IncrementalLaplacian::apply_rewrite). If the
    // node set is stable (dense path), performs a rank-k update (O(k^2 * n));
    // otherwise rebuilds from scratch. For n >= SPARSE_THRESHOLD, always
    // refactors from scratch via sparse LDL^T.
    //
    // `g_before` and `preserved` are part of the ported signature but unused in
    // the body (the Rust reference discards them with `let _ = (...)`): the
    // rewrite reads only the post-state graph.
    template <class PreservedSet>
    [[nodiscard]] std::expected<void, LdltErrorInfo>
    apply_rewrite(const aleph::graph::Graph& g_before,
                  const aleph::graph::Graph& g_after,
                  const PreservedSet& preserved) {
        (void)g_before;
        (void)preserved;
        WeightedLaplacian after_l = build_laplacian(g_after, default_weight);
        const std::size_t n = after_l.node_order.size();

        // SPARSE PATH: always refactor from scratch.
        if (n >= SPARSE_THRESHOLD) {
            const CsrMatrix csr = CsrMatrix::from_dense(after_l.matrix);
            auto f = SparseLdlt::factorize(csr);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor     = LaplacianFactor{std::in_place_index<1>, std::move(*f)};
            node_order = std::move(after_l.node_order);
            laplacian  = std::move(after_l.matrix);
            curvatures = std::move(after_l.curvatures);
            return {};
        }

        // DENSE PATH: preserve the original rank-k incremental logic.
        // If the node set changed, refactor from scratch.
        if (after_l.node_order != node_order) {
            auto f = LDLT::factorize(after_l.matrix);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor     = LaplacianFactor{std::in_place_index<0>, std::move(*f)};
            node_order = std::move(after_l.node_order);
            laplacian  = std::move(after_l.matrix);
            curvatures = std::move(after_l.curvatures);
            return {};
        }

        // Node set stable: Jacobi eigh + rank-k incremental update.
        // (n < SPARSE_THRESHOLD here, so the factor must be Dense; if it is not
        // — which the Rust reference treats as unreachable — refactor cleanly.)
        if (factor.index() != 0) {
            auto f = LDLT::factorize(after_l.matrix);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor     = LaplacianFactor{std::in_place_index<0>, std::move(*f)};
            laplacian  = std::move(after_l.matrix);
            curvatures = std::move(after_l.curvatures);
            return {};
        }

        LDLT          dense_factor = std::get<0>(factor);  // clone for the trial
        const DMatrix delta = after_l.matrix.add(laplacian.scale(-1.0));
        auto [eigvals, eigvecs] = detail::jacobi_eigh(delta);

        std::vector<f64>              alphas;
        std::vector<std::vector<f64>> cols;
        for (std::size_t i = 0; i < eigvals.size(); ++i) {
            const f64 lambda = eigvals[i];
            if (std::abs(lambda) > 1e-12) {
                std::vector<f64> col(eigvecs.rows(), 0.0);
                for (std::size_t r = 0; r < eigvecs.rows(); ++r) {
                    col[r] = eigvecs.at(r, i);
                }
                alphas.push_back(lambda);
                cols.push_back(std::move(col));
            }
        }

        // Try the incremental update; on failure, rebuild from scratch.
        LDLT trial = std::move(dense_factor);
        auto upd = trial.rank_k_update(std::span<const f64>(alphas),
                                       std::span<const std::vector<f64>>(cols));
        if (upd) {
            factor = LaplacianFactor{std::in_place_index<0>, std::move(trial)};
        } else {
            auto f = LDLT::factorize(after_l.matrix);
            if (!f) {
                return std::unexpected(f.error());
            }
            factor = LaplacianFactor{std::in_place_index<0>, std::move(*f)};
        }
        laplacian  = std::move(after_l.matrix);
        curvatures = std::move(after_l.curvatures);
        return {};
    }

    // Solve Delta x = b on the current state (port IncrementalLaplacian::solve).
    // Returns nullopt when b is not in the range of Delta (kernel guard).
    [[nodiscard]] std::optional<std::vector<f64>>
    solve(std::span<const f64> b) const {
        if (factor.index() == 0) {
            return std::get<0>(factor).solve(b);
        }
        return std::get<1>(factor).solve(b);
    }
};

}  // namespace aleph::flow
