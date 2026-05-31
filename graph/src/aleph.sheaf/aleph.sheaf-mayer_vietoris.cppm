// Numerical certificate that the Mayer-Vietoris dimension formula
//
//   dim H⁰(G') = dim H⁰(U) + dim H⁰(R) − dim H⁰(K) + ε_sheaf
//
// holds for a given rewrite, where ε_sheaf := rank(∂) is the rank of the Čech
// connecting morphism ∂: H⁰(F|_K) → H¹(F_{G'}) constructed in :connecting.
//
// `decompose_rewrite` splits a DPO rewrite into the value-type cover (U, K, R)
// of the post-rewrite host graph:
//   * K = discrete interface: exactly the preserved nodes, no edges.
//   * U = everything that survived (before-nodes minus deleted), with the
//     before-edges that re-appear in g_after by structural (src,dst,kind)
//     match.
//   * R = preserved ∪ created nodes, with the after-edges NOT claimed by U.
//
// `mayer_vietoris_certify[_with]` then builds the appropriate sheaf over each
// piece, computes the four H⁰ dims plus ε_sheaf = rank(∂), and reports the
// residual (which is 0 exactly when the identity holds).
//
// Ported from aleph-engine/aleph-sheaf/src/mayer_vietoris.rs. Oracle: the
// integer identity closes (residual == 0) on the canonical fixtures and the
// random rewrites; see aleph-engine/aleph-sheaf/tests/mayer_vietoris.rs and
// connecting_morphism.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust `decompose_rewrite(&Graph before, &Graph after, &IndexSet<NodeId>)`
//     returns `(Subgraph, Subgraph, Subgraph)` = (U, K, R). C++ mirrors this
//     with `std::tuple<Subgraph, Subgraph, Subgraph>` and takes the preserved
//     set as a `FlatSet<NodeId>` (the same set type the :subgraph node_ids use;
//     there is no OrderedSet, and insertion order is not load-bearing here —
//     K/U/R memberships are sorted sets).
//   * Rust tracks before/after node sets and U-claimed after-edges in
//     `IndexSet`s; C++ uses `FlatSet<NodeId>` / `FlatSet<EdgeId>` (sorted
//     membership sets — order is not observed, only membership/count). The
//     edge-claim loop preserves the Rust semantics exactly: each surviving
//     before-edge claims the first unclaimed after-edge of matching
//     (src,dst,kind) shape; R gets the after-edge complement.
//   * Rust `Subgraph` has public fields `node_ids` / `edge_ids` set via
//     `.insert(...)`. The C++ :subgraph keeps the same field names backed by
//     FlatSet, so `u.node_ids.insert(id)` / `.contains(id)` port verbatim.
//   * Rust `certify_via_trait` is generic over the `CellularZ2Sheaf` trait; the
//     C++ helper is a template over the `CellularZ2Sheaf` concept and delegates
//     to :cohomology `compute_hk` (H⁰) and a local `connecting_rank` for
//     ε_sheaf = rank(∂). `connecting_rank` reimplements :connecting's
//     `connecting_morphism` but performs the H¹ projection with the Rust
//     reference's greedy `reduce_modulo_image` semantics (a single pass over the
//     unreduced δ⁰ image columns) rather than the C++ gf2 `reduce_modulo_image`,
//     which is a *canonical* reduction that would collapse every δ⁰ column to 0
//     and force rank(∂) = 0. The Rust oracle (rank ∂ counts K-sections that do
//     not extend to global G' sections — e.g. 1 for a triangulated 4-cycle under
//     the constant sheaf) requires the partial residue, so we mirror it here
//     using only the faithful primitives (`coboundary_matrix`, `lift_k_to_g_prime`,
//     `image_basis`, `apply`, `from_cols`, `rank`).
//   * No exceptions (aleph_flags_isa): there are no recoverable-error paths in
//     this partition — `decompose_rewrite` and the certifier are total — so no
//     std::expected is needed; the per-piece sheaf builders assert on their own
//     preconditions (matching the sibling partitions).
//   * `residual` is `int` (Rust `i32`): the dims and ε are bounded by
//     stalk/component counts, so the signed subtraction is safe.

module;
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

export module aleph.sheaf:mayer_vietoris;

import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.gf2;

import :skeleton;
import :flag_complex;
import :subgraph;
import :sheaf_trait;
import :sheaf_constant;
import :sheaf_visibility;
import :cochain;
import :cohomology;
import :connecting;

export namespace aleph::sheaf {

// Which sheaf to build for the Mayer-Vietoris certificate.
enum class SheafKind {
    // The visibility sheaf: F(σ) = lights influencing σ.
    Visibility,
    // The constant Z/2 sheaf: F(σ) = Z/2 with identity restrictions.
    ConstantZ2,
};

// Decompose a DPO rewrite into Mayer-Vietoris pieces (U, K, R).
//
// Given the graph BEFORE the rewrite, the graph AFTER, and the runtime
// node-ids preserved by the rule (image of the rule interface K_pattern under
// the match), produce three Subgraph views over the post-rewrite host graph
// `g_after` such that:
//   * K is the discrete interface: exactly the preserved nodes, no edges.
//   * U covers everything that survived. Nodes = before-nodes minus deleted;
//     edges = before-edges that (i) have both endpoints in U and (ii) re-appear
//     in g_after by (src, dst, kind) shape (edge ids are re-issued by apply, so
//     we match structurally, claiming each after-edge at most once).
//   * R covers everything newly attached. Nodes = preserved ∪ created; edges =
//     the after-edges NOT claimed by U.
//
// Returns the tuple (U, K, R). Edge ids are runtime EdgeIds valid against
// `g_after` (the host of mayer_vietoris_certify). Ported from
// `decompose_rewrite`.
[[nodiscard]] inline std::tuple<Subgraph, Subgraph, Subgraph>
decompose_rewrite(const aleph::graph::Graph& g_before,
                  const aleph::graph::Graph& g_after,
                  const aleph::containers::FlatSet<aleph::types::NodeId>& preserved) {
    using aleph::types::EdgeId;
    using aleph::types::NodeId;

    aleph::containers::FlatSet<NodeId> before_nodes;
    for (auto [id, node] : g_before.nodes()) {
        (void)node;
        before_nodes.insert(id);
    }
    aleph::containers::FlatSet<NodeId> after_nodes;
    for (auto [id, node] : g_after.nodes()) {
        (void)node;
        after_nodes.insert(id);
    }

    // deleted = before \ after ; created = after \ before.
    aleph::containers::FlatSet<NodeId> deleted;
    for (NodeId n : before_nodes) {
        if (!after_nodes.contains(n)) deleted.insert(n);
    }
    aleph::containers::FlatSet<NodeId> created;
    for (NodeId n : after_nodes) {
        if (!before_nodes.contains(n)) created.insert(n);
    }

    // K = discrete interface: preserved nodes only, no edges.
    Subgraph k;
    for (NodeId n : preserved) k.node_ids.insert(n);

    // U.nodes = surviving nodes (before \ deleted) — the before-nodes still in
    // g_after.
    Subgraph u;
    for (NodeId n : before_nodes) {
        if (!deleted.contains(n)) u.node_ids.insert(n);
    }

    // U.edges (by g_after edge id): the before-edges that survived intact.
    // apply re-issues edge ids freely, so we match by (src, dst, kind). For
    // each before-edge with both endpoints in U.nodes, claim the first
    // unclaimed after-edge with matching shape; track claimed after-edges so R
    // can take the complement and two same-shape before-edges don't double-claim
    // a single after-edge.
    aleph::containers::FlatSet<EdgeId> after_claimed_by_u;
    for (auto [bid, e_before] : g_before.edges()) {
        (void)bid;
        if (!u.node_ids.contains(e_before.src) || !u.node_ids.contains(e_before.dst)) {
            continue;
        }
        for (auto [aid, e_after] : g_after.edges()) {
            if (after_claimed_by_u.contains(aid)) continue;
            if (e_after.src == e_before.src && e_after.dst == e_before.dst &&
                e_after.kind == e_before.kind) {
                after_claimed_by_u.insert(aid);
                u.edge_ids.insert(aid);
                break;  // first unclaimed match only
            }
        }
        // If no matching after-edge exists, the before-edge was structurally
        // deleted by the rule (its endpoints may survive) — it belongs to
        // neither U nor R.
    }

    // R.nodes = preserved ∪ created.
    Subgraph r;
    for (NodeId n : preserved) r.node_ids.insert(n);
    for (NodeId n : created) r.node_ids.insert(n);
    // R.edges = after-edges not claimed by U (touch a created node OR didn't
    // exist before by shape).
    for (auto [aid, e_after] : g_after.edges()) {
        (void)e_after;
        if (!after_claimed_by_u.contains(aid)) r.edge_ids.insert(aid);
    }

    return {std::move(u), std::move(k), std::move(r)};
}

// The Mayer-Vietoris certificate: the four H⁰ dims, ε_sheaf = rank(∂), and the
// residual `dim H⁰(G') − (dim H⁰(U) + dim H⁰(R) − dim H⁰(K) + ε_sheaf)`.
struct MayerVietorisCertificate {
    std::size_t h0_g_prime{0};
    std::size_t h0_u{0};
    std::size_t h0_r{0};
    std::size_t h0_k{0};
    // ε_sheaf := rank(∂), the rank of the Čech connecting morphism
    // H⁰(F|_K) → H¹(F_{G'}). Closes the M2.5 finding.
    std::size_t epsilon_sheaf{0};
    int         residual{0};

    [[nodiscard]] bool operator==(const MayerVietorisCertificate&) const noexcept = default;
};

}  // namespace aleph::sheaf

namespace aleph::sheaf::mayer_vietoris_detail {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;

// Index of the lowest set bit of `b`, or `b.size()` if `b` is all-zero.
[[nodiscard]] inline std::size_t lowest_set_bit(const BitVec& b) noexcept {
    const std::size_t n = b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (b.get(i)) return i;
    }
    return n;
}

// Project a C¹(G') element `c` (already in ker δ¹_{G'}) to its class in H¹(G'),
// modulo the image basis of δ⁰_{G'}.
//
// This faithfully replicates the Rust `BitMatrix::reduce_modulo_image`
// (aleph-engine/aleph-sheaf/src/linalg_gf2.rs): a SINGLE greedy pass over the
// *original, unreduced* image-basis columns, XORing each basis column `b` into
// `c` exactly when `c` carries a bit at `b`'s leading (lowest-set) position.
//
// The C++ gf2 `BitMatrix::reduce_modulo_image` instead builds a full echelon
// basis and performs a *canonical* reduction (zeroing every vector in the span).
// The two agree on a residue being zero iff the vector is in the span ONLY for
// single-pivot reductions; they diverge precisely on the connecting morphism,
// where δ⁰ columns lie in im(δ⁰) but the Mayer-Vietoris oracle (rank ∂ = the
// number of K-section classes that do NOT extend to global G' sections) requires
// the partial residue the Rust reference keeps. Mirroring the Rust algorithm
// here is what reproduces the oracle ε_sheaf (e.g. triangulating a 4-cycle under
// the constant sheaf yields rank ∂ = 1, not 0).
[[nodiscard]] inline BitVec project_to_h1_residue(
    BitVec c, std::span<const BitVec> d0_image_basis) {
    for (const BitVec& b : d0_image_basis) {
        const std::size_t pivot = lowest_set_bit(b);
        if (pivot < b.size() && c.get(pivot)) {
            c ^= b;
        }
    }
    return c;
}

// ε_sheaf = rank(∂), the rank of the Čech connecting morphism
// ∂: H⁰(F|_K) → H¹(F_{G'}).
//
// Reimplements aleph-engine/aleph-sheaf/src/connecting.rs::connecting_morphism
// locally so the projection step uses the Rust-faithful residue above rather
// than the C++ gf2 canonical reduction. Everything else (the H⁰(K) kernel basis,
// the lift via :connecting `lift_k_to_g_prime`, δ⁰ and its image basis) reuses
// the faithful sibling-partition primitives unchanged.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] std::size_t connecting_rank(
    const aleph::sheaf::FlagComplex& g_complex, const S& g_sheaf,
    const aleph::sheaf::FlagComplex& k_complex, const S& k_sheaf) {
    // Columns of ∂ are a basis of H⁰(K) = ker δ⁰_K (δ^{-1} := 0).
    const std::vector<BitVec> h0_k_basis =
        aleph::sheaf::coboundary_matrix(k_sheaf, k_complex, 0).kernel_basis();
    if (h0_k_basis.empty()) {
        return 0;  // no H⁰(K) classes to map → ∂ is the zero map from 0 dims.
    }

    const aleph::sheaf::CochainLayout k_layout =
        aleph::sheaf::CochainLayout::for_dim(k_sheaf, k_complex, 0);
    const aleph::sheaf::CochainLayout g_layout_0 =
        aleph::sheaf::CochainLayout::for_dim(g_sheaf, g_complex, 0);
    const BitMatrix d0 = aleph::sheaf::coboundary_matrix(g_sheaf, g_complex, 0);
    const std::vector<BitVec> d0_image_cols = d0.image_basis();
    const std::span<const BitVec> image_span{d0_image_cols};

    const std::size_t target_rows = d0.rows();  // dim C¹(G')
    std::vector<BitVec> cols;
    cols.reserve(h0_k_basis.size());
    for (const BitVec& k_class : h0_k_basis) {
        BitVec s_g = aleph::sheaf::lift_k_to_g_prime(k_class, k_layout, k_sheaf,
                                                     g_layout_0, g_sheaf);
        BitVec c = d0.apply(s_g);
        cols.push_back(project_to_h1_residue(std::move(c), image_span));
    }
    return BitMatrix::from_cols(std::span<const BitVec>(cols), target_rows).rank();
}

// Generic certify core: given the four complexes and the four sheaves, compute
// the H⁰ dims, ε_sheaf = rank(∂), and the residual. Ported from
// `certify_via_trait`. Templated on the CellularZ2Sheaf concept (Rust trait
// generic).
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] aleph::sheaf::MayerVietorisCertificate certify_via_trait(
    const aleph::sheaf::FlagComplex& gp_complex, const S& gp_sheaf,
    const aleph::sheaf::FlagComplex& u_complex, const S& u_sheaf,
    const aleph::sheaf::FlagComplex& r_complex, const S& r_sheaf,
    const aleph::sheaf::FlagComplex& k_complex, const S& k_sheaf) {
    const std::size_t h0_gp = aleph::sheaf::compute_hk(gp_sheaf, gp_complex, 0).dim;
    const std::size_t h0_u  = aleph::sheaf::compute_hk(u_sheaf, u_complex, 0).dim;
    const std::size_t h0_r  = aleph::sheaf::compute_hk(r_sheaf, r_complex, 0).dim;
    const std::size_t h0_k  = aleph::sheaf::compute_hk(k_sheaf, k_complex, 0).dim;

    const std::size_t epsilon_sheaf =
        connecting_rank(gp_complex, gp_sheaf, k_complex, k_sheaf);

    // dim_* and epsilon_sheaf are bounded by stalk/component counts; the signed
    // residual is safe for all realistic scene graphs (Rust uses i32).
    const int residual =
        static_cast<int>(h0_gp) -
        (static_cast<int>(h0_u) + static_cast<int>(h0_r) - static_cast<int>(h0_k) +
         static_cast<int>(epsilon_sheaf));

    return aleph::sheaf::MayerVietorisCertificate{
        .h0_g_prime    = h0_gp,
        .h0_u          = h0_u,
        .h0_r          = h0_r,
        .h0_k          = h0_k,
        .epsilon_sheaf = epsilon_sheaf,
        .residual      = residual,
    };
}

}  // namespace aleph::sheaf::mayer_vietoris_detail

export namespace aleph::sheaf {

// Kind-dispatched certifier: builds the appropriate sheaf over G' and over each
// cover piece, then delegates to the generic certify core. Ported from
// `mayer_vietoris_certify_with`.
[[nodiscard]] inline MayerVietorisCertificate mayer_vietoris_certify_with(
    const aleph::graph::Graph& g_prime, const Subgraph& u, const Subgraph& r,
    const Subgraph& k, SheafKind kind) {
    const OneSkeleton gp_skel = OneSkeleton::from_graph(g_prime);
    const FlagComplex gp_complex = build_flag_complex(gp_skel);
    auto [u_skel, u_complex] = u.flag_complex(g_prime);
    auto [r_skel, r_complex] = r.flag_complex(g_prime);
    auto [k_skel, k_complex] = k.flag_complex(g_prime);
    (void)u_skel;
    (void)r_skel;
    (void)k_skel;

    switch (kind) {
        case SheafKind::Visibility: {
            const VisibilitySheaf gp_sheaf = VisibilitySheaf::build_full(g_prime, gp_complex);
            const VisibilitySheaf u_sheaf =
                VisibilitySheaf::build_full_from_subgraph(g_prime, u, u_complex);
            const VisibilitySheaf r_sheaf =
                VisibilitySheaf::build_full_from_subgraph(g_prime, r, r_complex);
            const VisibilitySheaf k_sheaf =
                VisibilitySheaf::build_full_from_subgraph(g_prime, k, k_complex);
            return mayer_vietoris_detail::certify_via_trait(
                gp_complex, gp_sheaf, u_complex, u_sheaf, r_complex, r_sheaf, k_complex,
                k_sheaf);
        }
        case SheafKind::ConstantZ2: {
            const ConstantZ2Sheaf gp_sheaf{gp_complex};
            const ConstantZ2Sheaf u_sheaf{u_complex};
            const ConstantZ2Sheaf r_sheaf{r_complex};
            const ConstantZ2Sheaf k_sheaf{k_complex};
            return mayer_vietoris_detail::certify_via_trait(
                gp_complex, gp_sheaf, u_complex, u_sheaf, r_complex, r_sheaf, k_complex,
                k_sheaf);
        }
    }
    // Unreachable: SheafKind is exhaustively handled above.
    return MayerVietorisCertificate{};
}

// Backwards-compatible wrapper defaulting to the visibility sheaf. Ported from
// `mayer_vietoris_certify`.
[[nodiscard]] inline MayerVietorisCertificate mayer_vietoris_certify(
    const aleph::graph::Graph& g_prime, const Subgraph& u, const Subgraph& r,
    const Subgraph& k) {
    return mayer_vietoris_certify_with(g_prime, u, r, k, SheafKind::Visibility);
}

}  // namespace aleph::sheaf
