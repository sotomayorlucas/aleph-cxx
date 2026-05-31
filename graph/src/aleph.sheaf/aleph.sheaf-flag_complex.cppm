// The flag complex of the (Mesh, Adjacent) 1-skeleton.
//
// A k-simplex is a (k+1)-clique of mutually Adjacent meshes. Enumeration
// goes via Bron-Kerbosch with pivot for maximal cliques, then each maximal
// clique contributes all its non-empty subsets. Ported from
// aleph-engine/aleph-sheaf/src/flag_complex.rs.
//
// Adaptation notes (vs. the Rust reference):
//   * Rust keeps `simplices[k]` as an `IndexSet<Simplex>` (insertion-ordered,
//     set-deduped). C++26 has no OrderedSet, and per the 4c plan the levels are
//     `std::vector<std::vector<Simplex>>` sorted by `SimplexLess` and deduped.
//     The set of k-simplices is unaffected by pivot tie-breaking, so the
//     sorted+deduped output is fully deterministic regardless of iteration
//     order in Bron-Kerbosch.
//   * Bron-Kerbosch's R/P/X sets use sorted std::vector<NodeId> (NodeId has
//     operator< but no std::set-friendly <=> requirement here); the adjacency
//     is a sorted vector of (vertex, sorted-neighbours) entries so lookups and
//     iteration are deterministic.
//   * Programmer-error guard (clique size < 32) uses assert, matching the
//     no-exceptions ISA build (aleph_flags_isa).

module;
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module aleph.sheaf:flag_complex;

import aleph.types;
import :simplex;
import :skeleton;

export namespace aleph::sheaf {

// FlagComplex: the all-dimensional clique closure of the 1-skeleton.
//
//   max_dim       Highest dimension present. 0 if only vertices, 1 if some
//                 edges, etc. Convention: max_dim == 0 even when simplices[0]
//                 is empty (no vertices).
//   simplices[k]  All k-simplices as canonical Simplex values, sorted by
//                 SimplexLess and deduplicated.
struct FlagComplex {
    std::size_t                          max_dim{0};
    std::vector<std::vector<Simplex>>    simplices{};

    bool operator==(const FlagComplex& o) const = default;
};

}  // namespace aleph::sheaf

namespace aleph::sheaf::detail {

using aleph::types::NodeId;

// A sorted (vertex, sorted-neighbours) adjacency list. Vertices appear in
// ascending NodeId order; each neighbour list is ascending. This replaces the
// Rust IndexMap<NodeId, IndexSet<NodeId>>; ordering is deterministic.
struct Adjacency {
    std::vector<NodeId>                vertices;   // ascending, unique
    std::vector<std::vector<NodeId>>   neighbours; // parallel to `vertices`

    // Index of `v` in `vertices`, or SIZE_MAX if absent.
    [[nodiscard]] std::size_t index_of(NodeId v) const noexcept {
        auto it = std::lower_bound(vertices.begin(), vertices.end(), v);
        if (it == vertices.end() || *it != v) return static_cast<std::size_t>(-1);
        return static_cast<std::size_t>(it - vertices.begin());
    }

    [[nodiscard]] const std::vector<NodeId>& neighbours_of(NodeId v) const noexcept {
        const std::size_t i = index_of(v);
        assert(i != static_cast<std::size_t>(-1) && "neighbours_of: unknown vertex");
        return neighbours[i];
    }

    [[nodiscard]] bool adjacent(NodeId u, NodeId w) const noexcept {
        const auto& ns = neighbours_of(u);
        auto it = std::lower_bound(ns.begin(), ns.end(), w);
        return it != ns.end() && *it == w;
    }
};

inline bool sorted_contains(const std::vector<NodeId>& s, NodeId v) noexcept {
    auto it = std::lower_bound(s.begin(), s.end(), v);
    return it != s.end() && *it == v;
}

inline void sorted_insert(std::vector<NodeId>& s, NodeId v) {
    auto it = std::lower_bound(s.begin(), s.end(), v);
    if (it == s.end() || *it != v) s.insert(it, v);
}

inline void sorted_remove(std::vector<NodeId>& s, NodeId v) {
    auto it = std::lower_bound(s.begin(), s.end(), v);
    if (it != s.end() && *it == v) s.erase(it);
}

// Build the symmetric adjacency over skeleton vertices. Every vertex gets an
// entry (possibly with no neighbours); each Adjacent edge links both ends.
inline Adjacency build_adjacency(const aleph::sheaf::OneSkeleton& skel) {
    Adjacency adj;
    adj.vertices.reserve(skel.vertices.size());
    for (NodeId v : skel.vertices) sorted_insert(adj.vertices, v);
    adj.neighbours.assign(adj.vertices.size(), {});
    for (const auto& [a, b] : skel.edges) {
        const std::size_t ia = adj.index_of(a);
        const std::size_t ib = adj.index_of(b);
        assert(ia != static_cast<std::size_t>(-1) && "edge endpoint a not a vertex");
        assert(ib != static_cast<std::size_t>(-1) && "edge endpoint b not a vertex");
        sorted_insert(adj.neighbours[ia], b);
        sorted_insert(adj.neighbours[ib], a);
    }
    return adj;
}

// Count of vertices in `p` adjacent to `u`.
inline std::size_t neighbour_count_in(const Adjacency& adj, NodeId u,
                                      const std::vector<NodeId>& p) noexcept {
    std::size_t count = 0;
    for (NodeId n : adj.neighbours_of(u)) {
        if (sorted_contains(p, n)) ++count;
    }
    return count;
}

// Bron-Kerbosch with pivot. r/p/x are sorted vertex sets. Maximal cliques are
// appended to `out` as sorted vertex vectors.
inline void bk_recurse(std::vector<NodeId> r, std::vector<NodeId> p,
                       std::vector<NodeId> x, const Adjacency& adj,
                       std::vector<std::vector<NodeId>>& out) {
    if (p.empty() && x.empty()) {
        if (!r.empty()) out.push_back(std::move(r));
        return;
    }

    // Pivot = vertex in P∪X maximising |N(pivot) ∩ P|. Rust uses max_by_key
    // over p.iter().chain(x.iter()); we iterate P then X deterministically.
    bool         have_pivot = false;
    NodeId       pivot{0};
    std::size_t  best = 0;
    auto consider = [&](NodeId u) {
        const std::size_t c = neighbour_count_in(adj, u, p);
        if (!have_pivot || c >= best) {  // >= mirrors max_by_key's last-wins tie-break
            have_pivot = true;
            best       = c;
            pivot      = u;
        }
    };
    for (NodeId u : p) consider(u);
    for (NodeId u : x) consider(u);

    // Candidates = P \ N(pivot).
    std::vector<NodeId> candidates;
    candidates.reserve(p.size());
    for (NodeId v : p) {
        if (!adj.adjacent(pivot, v)) candidates.push_back(v);
    }

    for (NodeId v : candidates) {
        const auto& nbrs = adj.neighbours_of(v);

        std::vector<NodeId> r_new = r;
        sorted_insert(r_new, v);

        std::vector<NodeId> p_new;
        for (NodeId u : p) {
            if (sorted_contains(nbrs, u)) p_new.push_back(u);  // p sorted => p_new sorted
        }
        std::vector<NodeId> x_new;
        for (NodeId u : x) {
            if (sorted_contains(nbrs, u)) x_new.push_back(u);  // x sorted => x_new sorted
        }

        bk_recurse(std::move(r_new), std::move(p_new), std::move(x_new), adj, out);

        sorted_remove(p, v);
        sorted_insert(x, v);
    }
}

inline std::vector<std::vector<NodeId>> bron_kerbosch(const Adjacency& adj) {
    std::vector<std::vector<NodeId>> out;
    std::vector<NodeId>              r;
    std::vector<NodeId>              p = adj.vertices;  // already ascending/unique
    std::vector<NodeId>              x;
    bk_recurse(std::move(r), std::move(p), std::move(x), adj, out);
    return out;
}

// All non-empty subsets of `verts`, by ascending bitmask (mirrors the Rust
// 1..(1<<n) loop). Each subset preserves the input order of `verts`.
inline std::vector<std::vector<NodeId>> non_empty_subsets(const std::vector<NodeId>& verts) {
    const std::size_t n = verts.size();
    assert(n < 32 && "flag complex subset enumeration limited to cliques of size <32");
    std::vector<std::vector<NodeId>> out;
    out.reserve((static_cast<std::size_t>(1) << n) - 1);
    for (std::uint32_t mask = 1u; mask < (1u << n); ++mask) {
        std::vector<NodeId> subset;
        for (std::size_t i = 0; i < n; ++i) {
            if ((mask & (1u << i)) != 0u) subset.push_back(verts[i]);
        }
        out.push_back(std::move(subset));
    }
    return out;
}

}  // namespace aleph::sheaf::detail

export namespace aleph::sheaf {

// Build the flag complex of the 1-skeleton: 0-simplices are vertices,
// 1-simplices are Adjacent edges, k-simplices for k>=2 are (k+1)-cliques.
// Each level is sorted by SimplexLess and deduplicated (determinism).
[[nodiscard]] inline FlagComplex build_flag_complex(const OneSkeleton& skel) {
    const detail::Adjacency adj     = detail::build_adjacency(skel);
    const auto              maximal = detail::bron_kerbosch(adj);

    std::vector<std::vector<Simplex>> by_dim;
    by_dim.emplace_back();  // level 0 always exists
    for (aleph::types::NodeId v : skel.vertices) {
        by_dim[0].push_back(make_simplex(std::vector<aleph::types::NodeId>{v}));
    }

    for (const auto& clique : maximal) {
        for (auto& subset : detail::non_empty_subsets(clique)) {
            const std::size_t d = subset.size() - 1;
            while (by_dim.size() <= d) by_dim.emplace_back();
            by_dim[d].push_back(make_simplex(std::move(subset)));
        }
    }

    // Canonicalise each level: sort by SimplexLess, then dedup. This collapses
    // the Rust IndexSet semantics into a deterministic sorted vector.
    const SimplexLess less{};
    for (auto& level : by_dim) {
        std::sort(level.begin(), level.end(), less);
        level.erase(std::unique(level.begin(), level.end()), level.end());
    }

    const std::size_t max_dim = by_dim.empty() ? 0 : (by_dim.size() - 1);
    return FlagComplex{max_dim, std::move(by_dim)};
}

}  // namespace aleph::sheaf
