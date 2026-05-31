// Cohomology of a cellular Z/2 sheaf over the scene's flag complex.
//
//   * `compute_h0` — H⁰ of the VisibilitySheaf on a 1-skeleton via union-find
//     over connected components, with per-component coherent/conflict light
//     bookkeeping (the M2 fast path). Concrete on VisibilitySheaf, matching the
//     Rust reference.
//   * `compute_hk<S>` — H^k of any CellularZ2Sheaf S over a FlagComplex via
//     rank-nullity of the coboundary operators:
//         dim H^k = nullity(δ^k) − rank(δ^{k-1}),  with δ^{-1} := 0.
//     Uses BitMatrix::rank / kernel_basis (Wave-0 gf2 ops).
//   * `compute_subgraph_h0` — the moved Subgraph H⁰ seam (Rust
//     `Subgraph::h0_with_dim`): induced 1-skeleton + a caller-supplied
//     restricted sheaf → H⁰. Lives here (not in :subgraph) to avoid a
//     :subgraph → :cohomology dependency inversion.
//
// Ported from aleph-engine/aleph-sheaf/src/cohomology.rs (+ the moved
// h0_with_dim from subgraph.rs). Oracle dims are verbatim from
// aleph-engine/aleph-sheaf/tests/cohomology_regression.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust `Component.vertices` is an `IndexSet<NodeId>` (insertion-ordered);
//     the first-inserted vertex seeds the coherent/union accumulators. C++ has
//     no OrderedSet, so `vertices` is a `std::vector<NodeId>` that preserves
//     insertion order (push-back when a root bucket first sees a vertex). The
//     coherent/conflict `LightSet`s reuse :sheaf_visibility's sorted-vector set
//     semantics + detail helpers (light_contains / light_insert / etc.).
//   * Rust buckets components in an `IndexMap<NodeId, Component>` keyed by the
//     union-find root, in first-seen order. C++ mirrors this with the move-only
//     `OrderedMap<NodeId, Component>` (insertion order is load-bearing for the
//     deterministic component sequence) and moves the values out at the end.
//   * `dim_contribution`'s `IndexSet<Vec<NodeId>>` of conflict signatures
//     becomes a sorted+deduped `std::vector<LightSet>` — only the *count* of
//     distinct classes is observed, so order does not matter, but dedup must.
//   * `compute_hk` is generic over the C++26 `CellularZ2Sheaf` concept. The
//     CochainLayout `total_bits` and the generic coboundary matrix are computed
//     inline here (private detail helpers mirroring cochain.rs's generic
//     `coboundary_matrix` / `CochainLayout::for_dim`) so :cohomology stays
//     self-contained over the already-landed combinatorial + sheaf partitions
//     and the gf2 BitMatrix. The math is identical to the Rust reference.
//   * gf2 API drift: Rust `BitMatrix{rows,cols,get,set,rank,kernel_basis}` →
//     C++ `BitMatrix{rows(),cols(),at(),set(),rank(),kernel_basis()}`. `BitVec`
//     is the gf2 bit vector (kernel-basis element type).
//   * No exceptions (aleph_flags_isa): the Rust `expect`/index-panics on missing
//     stalks/simplices become `assert` (programmer-error guards), matching the
//     sibling partitions and Graph::insert_node's std::abort.

module;
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

export module aleph.sheaf:cohomology;

import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.gf2;

import :simplex;
import :skeleton;
import :flag_complex;
import :subgraph;
import :union_find;
import :sheaf_trait;
import :sheaf_visibility;

// Sorted-LightSet set helpers. :sheaf_visibility has equivalents in its own
// (non-exported) detail namespace, so they are not visible across the module
// boundary; :cohomology keeps its own copies. A LightSet is a sorted-ascending,
// deduped std::vector<NodeId> (the C++ stand-in for the Rust IndexSet<NodeId>).
namespace aleph::sheaf::cohomology_detail {

using aleph::types::NodeId;

// Membership in a sorted LightSet.
[[nodiscard]] inline bool light_contains(const aleph::sheaf::LightSet& s,
                                         NodeId l) noexcept {
    auto it = std::lower_bound(s.begin(), s.end(), l);
    return it != s.end() && *it == l;
}

// Sorted-set insertion (no duplicates); keeps `s` ascending.
inline void light_insert(aleph::sheaf::LightSet& s, NodeId l) {
    auto it = std::lower_bound(s.begin(), s.end(), l);
    if (it == s.end() || *it != l) s.insert(it, l);
}

// Intersection of `acc` with `next` (both sorted ascending), preserving the
// ascending order of `acc`. Mirrors the Rust
// `acc.iter().copied().filter(|l| next.contains(l)).collect()`.
[[nodiscard]] inline aleph::sheaf::LightSet light_intersect(
    const aleph::sheaf::LightSet& acc, const aleph::sheaf::LightSet& next) {
    aleph::sheaf::LightSet out;
    out.reserve(acc.size());
    for (NodeId l : acc) {
        if (light_contains(next, l)) out.push_back(l);
    }
    return out;
}

}  // namespace aleph::sheaf::cohomology_detail

export namespace aleph::sheaf {

using aleph::linalg::gf2::BitMatrix;
using aleph::linalg::gf2::BitVec;
using aleph::types::NodeId;

// ── H⁰ via union-find (M2 fast path) ──────────────────────────────────────────

// One connected component of the 1-skeleton, with the lights coherent across
// all of its vertices and the lights that appear on some but not all of them.
struct Component {
    // Vertices of the component, in first-seen (insertion) order. The first
    // vertex seeds the coherent/union accumulators in `compute_h0`.
    std::vector<NodeId> vertices{};
    // Lights present in every vertex stalk of the component (the intersection),
    // sorted ascending by NodeId.
    LightSet coherent_lights{};
    // Lights present in the union but not in the intersection, sorted ascending.
    LightSet conflict_lights{};

    // Number of independent global-section classes this component contributes
    // to dim H⁰:
    //   * empty conflict set → one section (constant on the coherent set);
    //   * non-empty conflict set → one section per distinct conflict class —
    //     vertices grouped by which conflict lights touch their stalk.
    // Ported from Component::dim_contribution.
    [[nodiscard]] std::size_t dim_contribution(const VisibilitySheaf& sheaf) const {
        if (conflict_lights.empty()) {
            return 1;
        }
        std::vector<LightSet> classes;  // distinct conflict signatures
        for (const NodeId v : vertices) {
            const LightSet& stalk = sheaf.stalk_at(make_simplex({v}));
            // Signature = the conflict lights that touch this vertex's stalk,
            // sorted ascending (conflict_lights is already ascending, so the
            // order-preserving filter keeps the signature sorted).
            LightSet signature;
            for (const NodeId l : conflict_lights) {
                if (cohomology_detail::light_contains(stalk, l)) signature.push_back(l);
            }
            // Insert into the distinct-signature set (only the count matters).
            const bool present =
                std::find(classes.begin(), classes.end(), signature) != classes.end();
            if (!present) classes.push_back(std::move(signature));
        }
        return classes.size();
    }
};

// H⁰ of the sheaf on a 1-skeleton: the connected components plus their light
// bookkeeping. `dim(sheaf)` sums the per-component section-class contributions.
struct H0 {
    std::vector<Component> components{};

    // dim H⁰ = Σ_component dim_contribution. Ported from H0::dim.
    [[nodiscard]] std::size_t dim(const VisibilitySheaf& sheaf) const {
        std::size_t total = 0;
        for (const Component& c : components) total += c.dim_contribution(sheaf);
        return total;
    }
};

// Compute H⁰ of `sheaf` over `skel` via union-find over the 1-skeleton edges,
// bucketing vertices by component root and computing each component's coherent
// (intersection) and conflict (union∖intersection) light sets. Ported from
// `compute_h0`.
[[nodiscard]] inline H0 compute_h0(const OneSkeleton& skel, const VisibilitySheaf& sheaf) {
    UnionFind uf;
    for (const NodeId v : skel.vertices) uf.make_set(v);
    for (const auto& [a, b] : skel.edges) uf.union_(a, b);

    // Bucket vertices by component root, in first-seen order. OrderedMap is
    // move-only (fine: we move the Components out below) and preserves the
    // insertion order the Rust IndexMap relied on for a deterministic component
    // sequence.
    aleph::containers::OrderedMap<NodeId, Component> buckets;
    for (const NodeId v : skel.vertices) {
        const NodeId root = uf.find(v);
        Component* entry = buckets.get_mut(root);
        if (entry == nullptr) {
            buckets.insert(root, Component{});
            entry = buckets.get_mut(root);
            assert(entry != nullptr && "bucket insert then get must succeed");
        }
        entry->vertices.push_back(v);
    }

    // For each component: coherent = ⋂ vertex stalks; union = ⋃ vertex stalks;
    // conflict = union ∖ coherent.
    for (auto [root, component] : buckets) {
        (void)root;
        assert(!component.vertices.empty() && "component has at least one vertex");
        const NodeId first = component.vertices.front();
        LightSet coherent = sheaf.stalk_at(make_simplex({first}));  // copy
        LightSet uni      = coherent;                                // union seed
        for (std::size_t i = 1; i < component.vertices.size(); ++i) {
            const LightSet& stalk = sheaf.stalk_at(make_simplex({component.vertices[i]}));
            coherent = cohomology_detail::light_intersect(coherent, stalk);
            for (const NodeId l : stalk) cohomology_detail::light_insert(uni, l);
        }
        // conflict = lights in the union but not in the coherent intersection,
        // preserving the ascending order of the union.
        LightSet conflict;
        for (const NodeId l : uni) {
            if (!cohomology_detail::light_contains(coherent, l)) conflict.push_back(l);
        }
        component.coherent_lights = std::move(coherent);
        component.conflict_lights = std::move(conflict);
    }

    // Move the components out in insertion (first-seen-root) order.
    H0 h0;
    h0.components.reserve(buckets.size());
    for (auto [root, component] : buckets) {
        (void)root;
        h0.components.push_back(std::move(component));
    }
    return h0;
}

// ── H^k via rank-nullity (M3a) ────────────────────────────────────────────────

// The cohomology group H^k(complex, sheaf) over GF(2), computed via rank-
// nullity: dim H^k = dim ker δ^k − rank δ^{k-1}.
struct Hk {
    std::size_t         k{0};                 // cohomological degree
    std::size_t         dim{0};               // dim H^k = kernel_dim − rank_delta_prev
    std::size_t         dim_ck{0};            // dim C^k(F) (total bits in C^k)
    std::size_t         rank_delta_prev{0};   // rank δ^{k-1} (0 if k == 0)
    std::size_t         rank_delta_curr{0};   // rank δ^k
    std::vector<BitVec> kernel_basis{};       // a basis for ker δ^k as C^k vectors
};

}  // namespace aleph::sheaf

namespace aleph::sheaf::cohomology_detail {

// Total bit width of the cochain space C^k(F): Σ_{σ ∈ complex.simplices[k]}
// dim_stalk(σ). Mirrors CochainLayout::for_dim(...).total_bits. The per-simplex
// bit ranges are laid out by accumulating widths in the level's (SimplexLess-
// sorted) order; here we only need the total and the per-simplex starting
// offsets, recomputed on demand below.
template <typename S>
[[nodiscard]] std::size_t cochain_total_bits(const S& sheaf,
                                             const aleph::sheaf::FlagComplex& complex,
                                             std::size_t k) {
    std::size_t total = 0;
    if (k < complex.simplices.size()) {
        for (const aleph::sheaf::Simplex& sigma : complex.simplices[k]) {
            total += sheaf.dim_stalk(sigma);
        }
    }
    return total;
}

// Starting bit offset of each simplex in C^k's flat layout, in level order.
// out[i] is the cursor at which complex.simplices[k][i]'s stalk bits begin.
template <typename S>
[[nodiscard]] std::vector<std::size_t> cochain_offsets(
    const S& sheaf, const aleph::sheaf::FlagComplex& complex, std::size_t k) {
    std::vector<std::size_t> offsets;
    if (k >= complex.simplices.size()) return offsets;
    offsets.reserve(complex.simplices[k].size());
    std::size_t cursor = 0;
    for (const aleph::sheaf::Simplex& sigma : complex.simplices[k]) {
        offsets.push_back(cursor);
        cursor += sheaf.dim_stalk(sigma);
    }
    return offsets;
}

// Index of `sigma` within complex.simplices[k] (level order), or SIZE_MAX.
[[nodiscard]] inline std::size_t simplex_index_in_level(
    const aleph::sheaf::FlagComplex& complex, std::size_t k,
    const aleph::sheaf::Simplex& sigma) {
    if (k >= complex.simplices.size()) return static_cast<std::size_t>(-1);
    const aleph::sheaf::SimplexLess less{};
    const auto& level = complex.simplices[k];
    auto it = std::lower_bound(level.begin(), level.end(), sigma, less);
    if (it == level.end() || less(sigma, *it)) return static_cast<std::size_t>(-1);
    return static_cast<std::size_t>(it - level.begin());
}

// The coboundary operator δ^k: C^k → C^{k+1} over GF(2), as a BitMatrix with
// rows indexed by C^{k+1}'s flat layout and columns by C^k's. If k+1 is beyond
// the complex, the matrix has zero rows (empty target layout). Mirrors the
// generic `coboundary_matrix` in cochain.rs exactly. Generic over the
// CellularZ2Sheaf concept.
template <aleph::sheaf::CellularZ2Sheaf S>
[[nodiscard]] aleph::linalg::gf2::BitMatrix coboundary_matrix(
    const S& sheaf, const aleph::sheaf::FlagComplex& complex, std::size_t k) {
    const std::size_t src_bits = cochain_total_bits(sheaf, complex, k);
    const std::size_t tgt_bits = cochain_total_bits(sheaf, complex, k + 1);
    aleph::linalg::gf2::BitMatrix data(tgt_bits, src_bits);
    if (k + 1 >= complex.simplices.size()) {
        return data;
    }

    const std::vector<std::size_t> src_off = cochain_offsets(sheaf, complex, k);
    const std::vector<std::size_t> tgt_off = cochain_offsets(sheaf, complex, k + 1);

    const auto& tau_level = complex.simplices[k + 1];
    for (std::size_t ti = 0; ti < tau_level.size(); ++ti) {
        const aleph::sheaf::Simplex& tau = tau_level[ti];
        const std::size_t tau_start = tgt_off[ti];
        for (const aleph::sheaf::Simplex& sigma : aleph::sheaf::faces_of_dim(tau, k)) {
            const std::size_t si = simplex_index_in_level(complex, k, sigma);
            assert(si != static_cast<std::size_t>(-1) &&
                   "every k-face of a (k+1)-simplex is a k-simplex of the complex");
            const std::size_t sigma_start = src_off[si];
            const aleph::linalg::gf2::BitMatrix r = sheaf.restriction(sigma, tau);
            for (std::size_t i = 0; i < r.rows(); ++i) {
                for (std::size_t j = 0; j < r.cols(); ++j) {
                    if (r.at(i, j)) {
                        const std::size_t row = tau_start + i;
                        const std::size_t col = sigma_start + j;
                        // XOR a 1 in (faithful to the Rust !prev toggle: a face
                        // appears once, so this sets the bit).
                        data.set(row, col, !data.at(row, col));
                    }
                }
            }
        }
    }
    return data;
}

}  // namespace aleph::sheaf::cohomology_detail

export namespace aleph::sheaf {

// Compute H^k(complex, sheaf) via rank-nullity of the coboundary operators.
// Convention: δ^{-1} := 0 so that dim H^0 = dim ker δ^0. Ported from
// `compute_hk`.
template <CellularZ2Sheaf S>
[[nodiscard]] Hk compute_hk(const S& sheaf, const FlagComplex& complex, std::size_t k) {
    const std::size_t cochain_dim = cohomology_detail::cochain_total_bits(sheaf, complex, k);

    const BitMatrix d_curr   = cohomology_detail::coboundary_matrix(sheaf, complex, k);
    const std::size_t rank_curr = d_curr.rank();

    const std::size_t rank_prev =
        (k == 0) ? std::size_t{0}
                 : cohomology_detail::coboundary_matrix(sheaf, complex, k - 1).rank();

    // saturating subtraction (matches the Rust saturating_sub guards).
    const std::size_t kernel_dim     = (cochain_dim > rank_curr) ? cochain_dim - rank_curr : 0;
    const std::size_t cohomology_dim = (kernel_dim > rank_prev) ? kernel_dim - rank_prev : 0;

    Hk out;
    out.k               = k;
    out.dim             = cohomology_dim;
    out.dim_ck          = cochain_dim;
    out.rank_delta_prev = rank_prev;
    out.rank_delta_curr = rank_curr;
    out.kernel_basis    = d_curr.kernel_basis();
    return out;
}

// ── Subgraph H⁰ seam (moved from Subgraph::h0_with_dim) ──────────────────────

// Compute H⁰ of `subgraph` viewed through `host`, over a caller-supplied
// restricted `sheaf`. The induced 1-skeleton (Mesh vertices in node_ids +
// Adjacent edges in edge_ids with both endpoints selected) is rebuilt here via
// `Subgraph::one_skeleton`; the sheaf must already be restricted to that
// subgraph (e.g. `VisibilitySheaf::build_full_from_subgraph` or the
// 1-skeleton-only builder). Lives in :cohomology (not :subgraph) to avoid a
// :subgraph → :cohomology dependency inversion. Ported from the H⁰ computation
// in `Subgraph::h0_with_dim`.
[[nodiscard]] inline H0 compute_subgraph_h0(const Subgraph& subgraph,
                                            const aleph::graph::Graph& host,
                                            const VisibilitySheaf& sheaf) {
    const OneSkeleton skel = subgraph.one_skeleton(host);
    return compute_h0(skel, sheaf);
}

}  // namespace aleph::sheaf
