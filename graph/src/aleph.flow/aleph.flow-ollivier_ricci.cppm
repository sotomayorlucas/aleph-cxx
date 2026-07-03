module;
// Ollivier-Ricci (Lin-Lu-Yau alpha=0) curvature for graphs.
//
// For an edge (u, v), define the uniform measure mu_u on neighbours of u.
// The curvature is  kappa(u, v) = 1 - W_1(mu_u, mu_v) / d(u, v).
//
// Ported faithfully from aleph-engine/aleph-flow/src/ollivier_ricci.rs.
// The W_1 entry point uses the transportation simplex (:wasserstein1); the
// additive W_2^eps form uses log-domain Sinkhorn-Knopp (:wasserstein2) with a
// hard cap of 1000 iterations. Both restrict the support to the connected
// component containing the edge endpoints so cross-component INF distances in
// the global hop-count matrix never produce NaN.
//
// Determinism (spec 7.5): all f64, no parallelism, fixed column-major order,
// stable node order derived from the (sorted) OneSkeleton vertices.
//
// aleph_flags_isa: no exceptions. The W_1 estimator returns
// std::expected<f64, W1Error>; here the shapes are constructed consistently so
// the error case cannot arise, and we fall back to 0.0 defensively (the Rust
// reference unwraps unconditionally).
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

export module aleph.flow:ollivier_ricci;

import :wasserstein1;
import :wasserstein2;
import aleph.graph;
import aleph.types;
import aleph.sheaf;
import aleph.containers;
import aleph.linalg.sparse;
import aleph.math;

// Hash for the (NodeId, NodeId) key of the result OrderedMap. The container
// requires std::hash<K>; the standard library does not specialise it for
// std::pair, so we provide one here (combining the two uint32 ids).
template <>
struct std::hash<std::pair<aleph::types::NodeId, aleph::types::NodeId>> {
    std::size_t operator()(
        const std::pair<aleph::types::NodeId, aleph::types::NodeId>& k)
        const noexcept {
        const std::size_t h1 = std::hash<aleph::types::NodeId>{}(k.first);
        const std::size_t h2 = std::hash<aleph::types::NodeId>{}(k.second);
        // boost-style hash_combine.
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

namespace aleph::flow::detail {

using aleph::math::f64;
using aleph::linalg::sparse::DMatrix;
using aleph::types::NodeId;
using aleph::sheaf::OneSkeleton;

// Insert idx into a sorted-unique neighbour list (IndexSet-equivalent for the
// uniform measure; iteration order is irrelevant since each neighbour carries
// equal mass).
inline void neighbour_insert(std::vector<std::size_t>& set, std::size_t idx) {
    for (const std::size_t v : set) {
        if (v == idx) return;
    }
    set.push_back(idx);
}

// uniform_on_neighbors: mu over all n nodes, mass 1/k on each of node's k
// neighbours, or a Dirac at node itself when isolated.
inline std::vector<f64> uniform_on_neighbors(
    std::size_t node, const std::vector<std::vector<std::size_t>>& neighbors,
    std::size_t n) {
    std::vector<f64> mu(n, 0.0);
    const std::size_t k = neighbors[node].size();
    if (k == 0) {
        mu[node] = 1.0;
        return mu;
    }
    const f64 w = 1.0 / static_cast<f64>(k);
    for (const std::size_t nb : neighbors[node]) {
        mu[nb] = w;
    }
    return mu;
}

// Common preamble shared by the W_1 and W_2 entry points: stable node order,
// node->index map, symmetric neighbour lists, and the all-pairs hop-count
// distance matrix (BFS from each source). Returns the node list, neighbour
// lists, and the dense distance matrix.
struct SkeletonState {
    std::vector<NodeId>                   nodes;
    std::vector<std::vector<std::size_t>> neighbors;
    DMatrix                               dist;
};

// Resolve a node id to its index in the stable node order (st.nodes), or
// st.nodes.size() when absent. Linear scan matches the original loop body.
inline std::size_t node_index_of(const SkeletonState& st, NodeId id) {
    const std::size_t n = st.nodes.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (st.nodes[i] == id) return i;
    }
    return n;
}

// Single-edge Ollivier-Ricci W_1 curvature, factored verbatim out of the
// per-edge body of ricci_curvature_from_skeleton. Given a prebuilt
// SkeletonState `st` (over g_after's skeleton) and an edge (a, b), compute
// kappa(a, b) = 1 - W_1(mu_a, mu_b) / d(a, b) via the IDENTICAL uniform measure
// + connected-component support slice + wasserstein_1 path. Reusing the
// identical math makes a recomputed kappa bit-exact to the full build's kappa
// for that edge (the Tier-1 byte-equality contract). Returns 0.0 if either
// endpoint is absent from the skeleton (mirrors the loop's `continue`, which
// simply omitted the edge — callers building a fresh map must not insert it).
[[nodiscard]] inline f64 ricci_curvature_edge(const SkeletonState& st, NodeId a,
                                              NodeId b) {
    const std::size_t n  = st.nodes.size();
    const std::size_t ia = node_index_of(st, a);
    const std::size_t ib = node_index_of(st, b);
    if (ia == n || ib == n) {
        return 0.0;
    }
    // Restrict to the connected component containing ia, ib (all finite
    // distances). Avoids NaN from cross-component INF distances.
    std::vector<std::size_t> support;
    for (std::size_t k = 0; k < n; ++k) {
        if (std::isfinite(st.dist.at(ia, k)) &&
            std::isfinite(st.dist.at(ib, k))) {
            support.push_back(k);
        }
    }
    const std::size_t m = support.size();
    aleph::linalg::sparse::DMatrix sub_dist =
        aleph::linalg::sparse::DMatrix::zeros(m, m);
    for (std::size_t si = 0; si < m; ++si) {
        for (std::size_t sj = 0; sj < m; ++sj) {
            sub_dist.at(si, sj) = st.dist.at(support[si], support[sj]);
        }
    }
    const std::vector<f64> global_mu = uniform_on_neighbors(ia, st.neighbors, n);
    const std::vector<f64> global_nu = uniform_on_neighbors(ib, st.neighbors, n);
    std::vector<f64> mu(m);
    std::vector<f64> nu(m);
    for (std::size_t k = 0; k < m; ++k) {
        mu[k] = global_mu[support[k]];
        nu[k] = global_nu[support[k]];
    }
    const auto w1_res = wasserstein_1(std::span<const f64>(mu),
                                      std::span<const f64>(nu), sub_dist);
    const f64 w1   = w1_res.value_or(0.0);
    const f64 d_ab = st.dist.at(ia, ib);
    return (d_ab > 0.0) ? (1.0 - w1 / d_ab) : 0.0;
}

// Radius for the bounded-support curvature ball B_R(a, b). R=2 is provably
// sufficient: for an edge (a, b) the W_1 support is N(a) u N(b) (1-hop), and the
// geodesics d(i, j) <= 3 between support nodes have all intermediate nodes
// within 2 hops of {a, b} -> B_2 captures them exactly. Only the local `n` (ball
// size) differs from the global support, which is exactly what makes kappa_R a
// PURE FUNCTION of the local ball (no global-`n` perturbation drift).
inline constexpr int kCurvRadius = 2;

}  // namespace aleph::flow::detail

export namespace aleph::flow {

// Shared skeleton adjacency: the global vertex->dense-index map and symmetric
// neighbour lists, built ONCE per skeleton and threaded through every per-edge
// bounded-curvature call (spec 2026-07-03). Built by the SAME loops in the
// SAME order as the previous per-call code (index over skel.vertices, adj over
// skel.edges), so curvatures computed through it are bit-identical to the
// fresh-adjacency path.
struct SkeletonAdjacency {
    aleph::containers::OrderedMap<aleph::types::NodeId, std::size_t> index;
    std::vector<std::vector<std::size_t>>                            adj;
};

[[nodiscard]] inline SkeletonAdjacency build_adjacency(
    const aleph::sheaf::OneSkeleton& skel) {
    SkeletonAdjacency shared;
    const std::size_t gn = skel.vertices.size();
    for (std::size_t i = 0; i < gn; ++i) {
        shared.index.insert(skel.vertices[i], i);
    }
    shared.adj.assign(gn, std::vector<std::size_t>{});
    for (const auto& [ea, eb] : skel.edges) {
        const std::size_t* ia = shared.index.get(ea);
        const std::size_t* ib = shared.index.get(eb);
        if (ia != nullptr && ib != nullptr) {
            shared.adj[*ia].push_back(*ib);
            shared.adj[*ib].push_back(*ia);
        }
    }
    return shared;
}

}  // namespace aleph::flow

namespace aleph::flow::detail {

// build_local_state: a SkeletonState scoped to the radius-`radius` ball
// B_R(a, b) of the skeleton, the bounded-support core. FULLY DETERMINISTIC and
// depends ONLY on the induced subgraph of B_R(a, b): two graphs that agree on
// B_R(a, b) yield byte-identical states. This is what makes the bounded
// curvature kappa_R(a, b) = ricci_curvature_edge(build_local_state(...), a, b) a
// pure function of the local ball -> exact localization by construction.
//
// Mechanics:
//   1. BFS from {a, b} over skel's adjacency to `radius` hops -> the ball node
//      set, then SORT it canonically (ascending NodeId) for a deterministic
//      stable order independent of discovery order / graph layout.
//   2. neighbors[i] = the in-ball neighbours of ball node i (induced subgraph).
//      a's TRUE 1-hop neighbours are all in the ball (radius >= 1), so the
//      uniform measure mu_a / mu_b over N(a) / N(b) is the full 1-hop star.
//   3. dist = all-pairs hop-count BFS WITHIN the induced ball subgraph (NOT
//      sliced from a global all-pairs matrix). For R=2 this equals the global
//      geodesic among the support nodes (intermediates are in B_2), so kappa_R
//      matches the global geometry; the only difference is the local `n`.
//   4. nodes = the sorted ball ids; n = ball size.
[[nodiscard]] inline SkeletonState build_local_state(
    const OneSkeleton& skel, const SkeletonAdjacency& shared, NodeId a,
    NodeId b, int radius) {
    const std::size_t gn = skel.vertices.size();

    // Global node id -> dense index and symmetric adjacency, prebuilt ONCE per
    // skeleton by build_adjacency (identical construction order), instead of
    // O(V+E) per edge.
    const auto& g_index = shared.index;
    const auto& g_adj   = shared.adj;

    // BFS from {a, b} to `radius` hops -> ball (dense global indices). Both
    // endpoints seed at distance 0.
    std::vector<bool>        in_ball(gn, false);
    std::vector<std::size_t> frontier;
    for (const NodeId seed : {a, b}) {
        const std::size_t* is = g_index.get(seed);
        if (is != nullptr && !in_ball[*is]) {
            in_ball[*is] = true;
            frontier.push_back(*is);
        }
    }
    for (int hop = 0; hop < radius; ++hop) {
        std::vector<std::size_t> next;
        for (const std::size_t u : frontier) {
            for (const std::size_t v : g_adj[u]) {
                if (!in_ball[v]) {
                    in_ball[v] = true;
                    next.push_back(v);
                }
            }
        }
        frontier = std::move(next);
    }

    // Collect ball node ids and SORT canonically (deterministic, layout-
    // independent). Iterating skel.vertices (already sorted) preserves order.
    SkeletonState st;
    for (std::size_t i = 0; i < gn; ++i) {
        if (in_ball[i]) {
            st.nodes.push_back(skel.vertices[i]);
        }
    }
    const std::size_t n = st.nodes.size();

    // Local id -> local index (sorted ball order).
    aleph::containers::OrderedMap<NodeId, std::size_t> l_index;
    for (std::size_t i = 0; i < n; ++i) {
        l_index.insert(st.nodes[i], i);
    }

    // Induced-subgraph neighbours: each ball node's neighbours that are also in
    // the ball (insertion order mirrors build_state via neighbour_insert).
    st.neighbors.assign(n, std::vector<std::size_t>{});
    for (const auto& [ea, eb] : skel.edges) {
        const std::size_t* la = l_index.get(ea);
        const std::size_t* lb = l_index.get(eb);
        if (la != nullptr && lb != nullptr) {
            neighbour_insert(st.neighbors[*la], *lb);
            neighbour_insert(st.neighbors[*lb], *la);
        }
    }

    // All-pairs hop-count distance via BFS WITHIN the induced ball subgraph.
    st.dist = DMatrix::zeros(n, n);
    const f64 inf = std::numeric_limits<f64>::infinity();
    for (std::size_t src = 0; src < n; ++src) {
        std::vector<f64> d(n, inf);
        d[src] = 0.0;
        std::vector<std::size_t> queue{src};
        std::size_t head = 0;
        while (head < queue.size()) {
            const std::size_t u = queue[head++];
            for (const std::size_t v : st.neighbors[u]) {
                if (std::isinf(d[v])) {
                    d[v] = d[u] + 1.0;
                    queue.push_back(v);
                }
            }
        }
        for (std::size_t v = 0; v < n; ++v) {
            st.dist.at(src, v) = d[v];
        }
    }
    return st;
}

// Fresh-adjacency wrapper: builds the shared adjacency for a single call.
[[nodiscard]] inline SkeletonState build_local_state(const OneSkeleton& skel,
                                                     NodeId a, NodeId b,
                                                     int radius) {
    return build_local_state(skel, build_adjacency(skel), a, b, radius);
}

// ricci_curvature_edge_bounded: the bounded-support Ollivier-Ricci curvature
// kappa_R(a, b) = ricci_curvature_edge(build_local_state(skel, a, b, radius),
// a, b). Reuses the committed per-edge primitive with the LOCAL state, so it is
// a pure function of B_R(a, b) and localizes byte-exact by construction.
[[nodiscard]] inline f64 ricci_curvature_edge_bounded(
    const OneSkeleton& skel, const SkeletonAdjacency& shared, NodeId a,
    NodeId b, int radius) {
    return ricci_curvature_edge(build_local_state(skel, shared, a, b, radius),
                                a, b);
}

[[nodiscard]] inline f64 ricci_curvature_edge_bounded(const OneSkeleton& skel,
                                                      NodeId a, NodeId b,
                                                      int radius) {
    return ricci_curvature_edge_bounded(skel, build_adjacency(skel), a, b,
                                        radius);
}

inline SkeletonState build_state(const OneSkeleton& skel) {
    SkeletonState st;
    st.nodes.assign(skel.vertices.begin(), skel.vertices.end());
    const std::size_t n = st.nodes.size();

    // node -> index (stable, by skeleton vertex order).
    aleph::containers::OrderedMap<NodeId, std::size_t> node_index;
    for (std::size_t i = 0; i < n; ++i) {
        node_index.insert(st.nodes[i], i);
    }

    st.neighbors.assign(n, std::vector<std::size_t>{});
    for (const auto& [a, b] : skel.edges) {
        const std::size_t* ia = node_index.get(a);
        const std::size_t* ib = node_index.get(b);
        if (ia != nullptr && ib != nullptr) {
            neighbour_insert(st.neighbors[*ia], *ib);
            neighbour_insert(st.neighbors[*ib], *ia);
        }
    }

    // Hop-count graph distance via BFS from every source.
    st.dist = DMatrix::zeros(n, n);
    const f64 inf = std::numeric_limits<f64>::infinity();
    for (std::size_t src = 0; src < n; ++src) {
        std::vector<f64> d(n, inf);
        d[src] = 0.0;
        std::vector<std::size_t> queue{src};
        std::size_t head = 0;
        while (head < queue.size()) {
            const std::size_t u = queue[head++];
            for (const std::size_t v : st.neighbors[u]) {
                if (std::isinf(d[v])) {
                    d[v] = d[u] + 1.0;
                    queue.push_back(v);
                }
            }
        }
        for (std::size_t v = 0; v < n; ++v) {
            st.dist.at(src, v) = d[v];
        }
    }
    return st;
}

}  // namespace aleph::flow::detail

export namespace aleph::flow {

using aleph::math::f64;
using aleph::types::NodeId;
using aleph::sheaf::OneSkeleton;

// Result map: sorted node-id pair -> curvature, in skeleton-edge order.
using RicciMap =
    aleph::containers::OrderedMap<std::pair<NodeId, NodeId>, f64>;

// Compute Ollivier-Ricci W_1 curvature for every Adjacent edge in the
// 1-skeleton. Returns a map from sorted node-id pair to curvature.
[[nodiscard]] inline RicciMap ricci_curvature_from_skeleton(
    const OneSkeleton& skel) {
    RicciMap out;
    const std::size_t n = skel.vertices.size();
    if (n == 0) {
        return out;
    }
    const detail::SkeletonState st = detail::build_state(skel);

    for (const auto& [a, b] : skel.edges) {
        // Skip edges whose endpoints are absent from the node order (mirrors
        // the original `continue`); otherwise compute the single-edge kappa via
        // the factored primitive (identical math, byte-exact).
        if (detail::node_index_of(st, a) == n ||
            detail::node_index_of(st, b) == n) {
            continue;
        }
        const f64 kappa = detail::ricci_curvature_edge(st, a, b);
        out.insert(std::pair<NodeId, NodeId>{a, b}, kappa);
    }
    return out;
}

// Compute Ollivier-Ricci W_1 curvature for every Adjacent edge in the
// 1-skeleton derived from g.
[[nodiscard]] inline RicciMap ricci_curvature(const aleph::graph::Graph& g) {
    const OneSkeleton skel = OneSkeleton::from_graph(g);
    return ricci_curvature_from_skeleton(skel);
}

// Bounded-support kappa_R for a single edge, with a prebuilt SkeletonAdjacency
// (shared across many per-edge calls) or a fresh one per call. Radius defaults
// to the provably sufficient R=2 ball.
[[nodiscard]] inline f64 ricci_curvature_edge_bounded(
    const OneSkeleton& skel, const SkeletonAdjacency& shared, NodeId a,
    NodeId b, int radius = detail::kCurvRadius) {
    return detail::ricci_curvature_edge_bounded(skel, shared, a, b, radius);
}

[[nodiscard]] inline f64 ricci_curvature_edge_bounded(
    const OneSkeleton& skel, NodeId a, NodeId b,
    int radius = detail::kCurvRadius) {
    return detail::ricci_curvature_edge_bounded(skel, a, b, radius);
}

// Compute Ollivier-Ricci W_2^eps curvature (Sinkhorn-Knopp form) for every
// Adjacent edge in the 1-skeleton.
//
// kappa_w2(u, v) = 1 - W_2^eps(mu_u, mu_v) / d(u, v) where mu_x is the uniform
// measure on the neighbours of x. Uses Sinkhorn-Knopp with the given epsilon
// (typical: 0.05) and a hard cap of 1000 iterations. Additive — does NOT
// replace the W_1 entry points.
[[nodiscard]] inline RicciMap ricci_curvature_w2(const OneSkeleton& skel,
                                                 f64 epsilon) {
    RicciMap out;
    const std::size_t n = skel.vertices.size();
    if (n == 0) {
        return out;
    }
    const detail::SkeletonState st = detail::build_state(skel);

    for (const auto& [a, b] : skel.edges) {
        std::size_t ia = n;
        std::size_t ib = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (st.nodes[i] == a) ia = i;
            if (st.nodes[i] == b) ib = i;
        }
        if (ia == n || ib == n) {
            continue;
        }
        // Restrict to the connected component containing ia, ib.
        std::vector<std::size_t> support;
        for (std::size_t k = 0; k < n; ++k) {
            if (std::isfinite(st.dist.at(ia, k)) &&
                std::isfinite(st.dist.at(ib, k))) {
                support.push_back(k);
            }
        }
        const std::size_t m = support.size();
        // Squared-distance sub-cost matrix as vector<vector<f64>>.
        std::vector<std::vector<f64>> sub_cost_sq(m, std::vector<f64>(m, 0.0));
        for (std::size_t si = 0; si < m; ++si) {
            for (std::size_t sj = 0; sj < m; ++sj) {
                const f64 d_st = st.dist.at(support[si], support[sj]);
                sub_cost_sq[si][sj] = d_st * d_st;
            }
        }
        const std::vector<f64> global_mu =
            detail::uniform_on_neighbors(ia, st.neighbors, n);
        const std::vector<f64> global_nu =
            detail::uniform_on_neighbors(ib, st.neighbors, n);
        std::vector<f64> mu(m);
        std::vector<f64> nu(m);
        for (std::size_t k = 0; k < m; ++k) {
            mu[k] = global_mu[support[k]];
            nu[k] = global_nu[support[k]];
        }
        const f64 w2 =
            wasserstein2_sinkhorn(mu, nu, sub_cost_sq, epsilon, 1000);
        const f64 d_ab = st.dist.at(ia, ib);
        const f64 kappa = (d_ab > 0.0) ? (1.0 - w2 / d_ab) : 0.0;
        out.insert(std::pair<NodeId, NodeId>{a, b}, kappa);
    }
    return out;
}

}  // namespace aleph::flow
