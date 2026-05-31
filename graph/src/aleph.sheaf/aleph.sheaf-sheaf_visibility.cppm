// The visibility sheaf F over the flag complex of (Mesh, Adjacent).
//
//   F(σ) = ⋂_{v ∈ σ} { lights l with l —Influences→ v }
//
// Vertex stalks are the lights influencing each mesh vertex; higher-dim stalks
// are the pointwise intersection of their face (vertex) stalks. Restriction
// maps are inclusions F(σ) ⊆ F(τ) rendered as 0/1 BitMatrices. Ported from
// aleph-engine/aleph-sheaf/src/sheaf.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust stalks are `IndexMap<Simplex, IndexSet<NodeId>>` (insertion-ordered
//     map + ordered set). C++26 has no OrderedSet, so each stalk is a sorted,
//     deduped `std::vector<NodeId>` (a light *set*; the visibility sheaf sorts
//     ascending anyway via `LightSet::sort`). The stalk *container* is
//     `OrderedMap<Simplex, LightSet>` — that map is move-only, which is fine
//     because a VisibilitySheaf is never cloned (it is rebuilt from a graph).
//   * Stalks are ordered ascending by NodeId, mirroring the Rust `out.sort()`
//     in `lights_influencing` and the order-preserving `filter().collect()`
//     used to intersect; both endpoints therefore agree on basis-element index.
//   * `restriction`'s "F(τ) ⊆ F(σ) by construction" expectation and the
//     `stalk[sigma]` index that panic in Rust become `assert` here: they are
//     programmer-error guards under the no-exceptions ISA build, matching
//     Graph::insert_node's `std::abort` on a precondition violation.
//   * `stalk_dim` is renamed `dim_stalk` per the 4c plan (the sheaf-concept
//     method names shared across :sheaf_trait and cluster C).
//   * `lift_basis_index` returns `std::optional<std::size_t>` (Rust `Option`).

module;
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <vector>

export module aleph.sheaf:sheaf_visibility;

import aleph.graph;
import aleph.types;
import aleph.containers;
import aleph.linalg.gf2;
import :simplex;
import :skeleton;
import :flag_complex;
import :subgraph;

export namespace aleph::sheaf {

// A stalk: the set of lights at a simplex, sorted ascending by NodeId and
// deduped. There is no OrderedSet in aleph.containers, so a sorted vector
// carries the set semantics the Rust `IndexSet<NodeId>` relied on (membership,
// count, and a stable index per light).
using LightSet = std::vector<aleph::types::NodeId>;

}  // namespace aleph::sheaf

namespace aleph::sheaf::detail {

// Membership in a sorted LightSet.
[[nodiscard]] inline bool light_contains(const LightSet& s,
                                         aleph::types::NodeId l) noexcept {
    auto it = std::lower_bound(s.begin(), s.end(), l);
    return it != s.end() && *it == l;
}

// Index of light `l` in a sorted LightSet, or SIZE_MAX if absent. Mirrors the
// Rust `IndexSet::get_index_of`.
[[nodiscard]] inline std::size_t light_index_of(const LightSet& s,
                                                aleph::types::NodeId l) noexcept {
    auto it = std::lower_bound(s.begin(), s.end(), l);
    if (it == s.end() || *it != l) return static_cast<std::size_t>(-1);
    return static_cast<std::size_t>(it - s.begin());
}

// Sorted-set insertion (no duplicates). Keeps `s` ascending.
inline void light_insert(LightSet& s, aleph::types::NodeId l) {
    auto it = std::lower_bound(s.begin(), s.end(), l);
    if (it == s.end() || *it != l) s.insert(it, l);
}

// Intersection of `acc` with `next` (both sorted, ascending), preserving the
// ascending order of `acc`. Mirrors the Rust
// `acc.iter().copied().filter(|l| next.contains(l)).collect()`.
[[nodiscard]] inline LightSet light_intersect(const LightSet& acc,
                                              const LightSet& next) {
    LightSet out;
    out.reserve(acc.size());
    for (aleph::types::NodeId l : acc) {
        if (light_contains(next, l)) out.push_back(l);
    }
    return out;
}

}  // namespace aleph::sheaf::detail

export namespace aleph::sheaf {

// Lights `l` with `l —Influences→ v` in `graph`, sorted ascending by NodeId.
// Only Light-kind sources count (an Influences edge can also originate from a
// Volume or Material per the edge typing rules — those are not lights).
[[nodiscard]] inline LightSet lights_influencing(const aleph::graph::Graph& graph,
                                                 aleph::types::NodeId v) {
    LightSet out;
    for (auto [eid, e] : graph.edges()) {
        (void)eid;
        if (e.kind == aleph::types::EdgeKind::Influences && e.dst == v) {
            const aleph::types::Node* src = graph.node(e.src);
            if (src != nullptr &&
                aleph::types::kind_of(*src) == aleph::types::NodeKind::Light) {
                detail::light_insert(out, e.src);
            }
        }
    }
    // `light_insert` already keeps `out` sorted ascending and deduped, matching
    // the Rust `out.sort()`.
    return out;
}

// The visibility sheaf: a stalk (LightSet) per simplex of the flag complex.
//
// Move-only (the backing OrderedMap is move-only); a VisibilitySheaf is built
// from a graph + complex and never copied. Satisfies the CellularZ2Sheaf
// concept via `dim_stalk` / `restriction` / `lift_basis_index`.
class VisibilitySheaf {
public:
    using StalkMap = aleph::containers::OrderedMap<Simplex, LightSet>;

    VisibilitySheaf() = default;

    // Build a sheaf over a full flag complex. Vertex stalks are
    // `lights_influencing(graph, v)`; higher-dim stalks are pointwise
    // intersections of their face stalks. Ported from `build_full`.
    [[nodiscard]] static VisibilitySheaf build_full(const aleph::graph::Graph& graph,
                                                    const FlagComplex& complex) {
        VisibilitySheaf sheaf;
        if (complex.simplices.empty()) return sheaf;

        for (const Simplex& sv : complex.simplices[0]) {
            sheaf.stalk_.insert(sv, lights_influencing(graph, sv[0]));
        }
        for (std::size_t k = 1; k <= complex.max_dim; ++k) {
            for (const Simplex& s : complex.simplices[k]) {
                LightSet acc = sheaf.copy_vertex_stalk(s[0]);
                for (std::size_t i = 1; i < s.size(); ++i) {
                    const LightSet* next = sheaf.vertex_stalk(s[i]);
                    assert(next != nullptr && "vertex stalk missing in build_full");
                    acc = detail::light_intersect(acc, *next);
                }
                sheaf.stalk_.insert(s, std::move(acc));
            }
        }
        return sheaf;
    }

    // M2-compatible builder that only populates 0- and 1-simplex stalks. Kept
    // for tests exercising the union-find H⁰ fast path. Ported from
    // `build_1_skeleton_only`.
    [[nodiscard]] static VisibilitySheaf build_1_skeleton_only(
        const aleph::graph::Graph& graph, const OneSkeleton& skel) {
        VisibilitySheaf sheaf;
        for (aleph::types::NodeId v : skel.vertices) {
            sheaf.stalk_.insert(make_simplex({v}), lights_influencing(graph, v));
        }
        for (const auto& [a, b] : skel.edges) {
            const LightSet* fa = sheaf.vertex_stalk(a);
            const LightSet* fb = sheaf.vertex_stalk(b);
            assert(fa != nullptr && fb != nullptr && "edge endpoint stalk missing");
            LightSet inter = detail::light_intersect(*fa, *fb);
            sheaf.stalk_.insert(make_simplex({a, b}), std::move(inter));
        }
        return sheaf;
    }

    // Build a sheaf over a flag complex restricted to a Subgraph view of a host
    // Graph. Influences edges and Light sources count only if their ids appear
    // in the subgraph's edge/node sets. Used by Mayer-Vietoris to build
    // F|_K, F|_U, F|_R. Ported from `build_full_from_subgraph`.
    [[nodiscard]] static VisibilitySheaf build_full_from_subgraph(
        const aleph::graph::Graph& host, const Subgraph& subgraph,
        const FlagComplex& complex) {
        VisibilitySheaf sheaf;
        if (complex.simplices.empty()) return sheaf;

        for (const Simplex& sv : complex.simplices[0]) {
            const aleph::types::NodeId v = sv[0];
            LightSet lights;
            for (auto [eid, e] : host.edges()) {
                if (!subgraph.edge_ids.contains(eid)) continue;
                if (e.kind == aleph::types::EdgeKind::Influences && e.dst == v &&
                    subgraph.node_ids.contains(e.src)) {
                    const aleph::types::Node* src = host.node(e.src);
                    if (src != nullptr &&
                        aleph::types::kind_of(*src) == aleph::types::NodeKind::Light) {
                        detail::light_insert(lights, e.src);
                    }
                }
            }
            sheaf.stalk_.insert(sv, std::move(lights));
        }
        for (std::size_t k = 1; k <= complex.max_dim; ++k) {
            for (const Simplex& s : complex.simplices[k]) {
                LightSet acc = sheaf.copy_vertex_stalk(s[0]);
                for (std::size_t i = 1; i < s.size(); ++i) {
                    const LightSet* next = sheaf.vertex_stalk(s[i]);
                    assert(next != nullptr && "vertex stalk missing in build_full_from_subgraph");
                    acc = detail::light_intersect(acc, *next);
                }
                sheaf.stalk_.insert(s, std::move(acc));
            }
        }
        return sheaf;
    }

    // Read access to the stalk (light set) at `sigma`. Precondition: sigma is
    // a simplex of the complex this sheaf was built over. Ported from
    // `stalk_at` (which indexes and panics on a missing key).
    [[nodiscard]] const LightSet& stalk_at(const Simplex& sigma) const {
        const LightSet* s = stalk_.get(sigma);
        assert(s != nullptr && "stalk_at: simplex not in sheaf");
        return *s;
    }

    // ── CellularZ2Sheaf concept surface ──────────────────────────────────

    // dim F(σ) = number of lights in the stalk. Missing simplices have a
    // 0-dimensional stalk (Rust `map_or(0, len)`).
    [[nodiscard]] std::size_t dim_stalk(const Simplex& sigma) const {
        const LightSet* s = stalk_.get(sigma);
        return (s == nullptr) ? std::size_t{0} : s->size();
    }

    // Restriction ρ_{σ→τ}: F(σ) → F(τ) for σ a face of τ, as a
    // dim_stalk(τ) × dim_stalk(σ) inclusion matrix. F(τ) ⊆ F(σ) holds by
    // construction (τ ⊇ σ ⇒ more vertices ⇒ smaller light intersection), so
    // every light of F(τ) has an index in F(σ). Ported from `restriction`.
    [[nodiscard]] aleph::linalg::gf2::BitMatrix restriction(const Simplex& sigma,
                                                            const Simplex& tau) const {
        const LightSet& f_sigma = stalk_at(sigma);
        const LightSet& f_tau   = stalk_at(tau);
        aleph::linalg::gf2::BitMatrix m(f_tau.size(), f_sigma.size());
        for (std::size_t i = 0; i < f_tau.size(); ++i) {
            const std::size_t j = detail::light_index_of(f_sigma, f_tau[i]);
            assert(j != static_cast<std::size_t>(-1) &&
                   "F(tau) subset of F(sigma) by construction in the visibility sheaf");
            m.set(i, j, true);
        }
        return m;
    }

    // Lift basis element `idx` of this sheaf's stalk at `sigma` into a basis
    // element index of `other`'s stalk at `sigma` (a supersheaf, same kind,
    // possibly larger stalks). Visibility uses Light NodeId alignment. Returns
    // nullopt if σ is absent in either sheaf or the light is not in `other`'s
    // stalk. Ported from `lift_basis_index`.
    [[nodiscard]] std::optional<std::size_t> lift_basis_index(
        const Simplex& sigma, std::size_t idx, const VisibilitySheaf& other) const {
        const LightSet* k_stalk = stalk_.get(sigma);
        if (k_stalk == nullptr || idx >= k_stalk->size()) return std::nullopt;
        const aleph::types::NodeId light = (*k_stalk)[idx];
        const LightSet* g_stalk = other.stalk_.get(sigma);
        if (g_stalk == nullptr) return std::nullopt;
        const std::size_t j = detail::light_index_of(*g_stalk, light);
        if (j == static_cast<std::size_t>(-1)) return std::nullopt;
        return j;
    }

    // Direct const access to the backing stalk map (move-only; not copyable).
    [[nodiscard]] const StalkMap& stalks() const noexcept { return stalk_; }

private:
    // Read the vertex (0-simplex) stalk for `v`, or nullptr if absent.
    [[nodiscard]] const LightSet* vertex_stalk(aleph::types::NodeId v) const {
        return stalk_.get(make_simplex({v}));
    }
    // Copy of the vertex stalk for `v` (the accumulator seed for an
    // intersection). Precondition: the vertex stalk exists.
    [[nodiscard]] LightSet copy_vertex_stalk(aleph::types::NodeId v) const {
        const LightSet* s = vertex_stalk(v);
        assert(s != nullptr && "vertex stalk missing");
        return *s;
    }

    StalkMap stalk_{};
};

}  // namespace aleph::sheaf
