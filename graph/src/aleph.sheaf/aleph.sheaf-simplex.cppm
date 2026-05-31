module;
#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

export module aleph.sheaf:simplex;

// Simplices of the flag complex.
//
// A k-simplex is a `std::vector<NodeId>` of length k+1, kept sorted ascending
// by `NodeId` so that two simplices with the same vertex set compare equal.
// Module-level helpers manage face enumeration without re-implementing the
// canonical-ordering logic at each call site.
//
// Ported from aleph-engine/aleph-sheaf/src/simplex.rs.

import aleph.types;

export namespace aleph::sheaf {

using aleph::types::NodeId;

// A simplex is a sorted/deduped vector of vertex ids. Use `make_simplex` to
// build one rather than constructing the vector directly — the canonical
// ascending order is what makes vertex-set equality coincide with `==`.
using Simplex = std::vector<NodeId>;

// Construct a `Simplex` from a span of `NodeId`, ordering its vertices
// ascending and removing duplicates.
[[nodiscard]] inline Simplex make_simplex(std::span<const NodeId> verts) {
    Simplex v(verts.begin(), verts.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// Convenience overload for braced initializer lists: `make_simplex({a, b, c})`.
[[nodiscard]] inline Simplex make_simplex(std::initializer_list<NodeId> verts) {
    return make_simplex(std::span<const NodeId>(verts.begin(), verts.size()));
}

// Dimension of a simplex: `len() - 1`. Vertices are 0-simplices, edges are
// 1-simplices, triangles are 2-simplices, etc. An empty simplex has dimension
// 0 (saturating subtraction, matching the Rust `saturating_sub`).
[[nodiscard]] inline std::size_t dim(const Simplex& s) noexcept {
    return s.empty() ? std::size_t{0} : s.size() - 1;
}

// Enumerate the `k`-faces of `s`: every subset of `s` of cardinality `k+1`.
// Order is the lexicographic order of the binomial index enumeration, which is
// deterministic. Returns an empty vector when `k+1 > |s|`.
[[nodiscard]] inline std::vector<Simplex> faces_of_dim(const Simplex& s, std::size_t k) {
    std::vector<Simplex> out;
    if (k + 1 > s.size()) {
        return out;
    }
    const std::size_t n = s.size();
    const std::size_t m = k + 1;
    std::vector<std::size_t> idx(m);
    for (std::size_t i = 0; i < m; ++i) idx[i] = i;
    for (;;) {
        Simplex face;
        face.reserve(m);
        for (std::size_t i = 0; i < m; ++i) face.push_back(s[idx[i]]);
        out.push_back(std::move(face));

        bool bumped = false;
        for (std::size_t jj = m; jj-- > 0;) {
            if (idx[jj] + 1 < n - (m - 1 - jj)) {
                idx[jj] += 1;
                for (std::size_t k2 = jj + 1; k2 < m; ++k2) {
                    idx[k2] = idx[k2 - 1] + 1;
                }
                bumped = true;
                break;
            }
        }
        if (!bumped) {
            break;
        }
    }
    return out;
}

// Strict-weak ordering on simplices, lexicographic on the (already sorted)
// vertex vectors. `NodeId` has `<` but no `<=>`, so we order by hand. Use this
// for `FlatSet<Simplex, SimplexLess>` and any sorted-vector set of simplices.
struct SimplexLess {
    [[nodiscard]] bool operator()(const Simplex& a, const Simplex& b) const noexcept {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](const NodeId& x, const NodeId& y) noexcept { return x < y; });
    }
};

}  // namespace aleph::sheaf

// Hash specialization so `OrderedMap<Simplex, V>` (which calls `std::hash<K>`)
// can key on simplices. Because `make_simplex` canonicalizes ordering, two
// simplices with the same vertex set hash identically. Placed in the global
// fragment outside the export namespace, mirroring `std::hash<NodeId>`.
template <>
struct std::hash<aleph::sheaf::Simplex> {
    std::size_t operator()(const aleph::sheaf::Simplex& s) const noexcept {
        // FNV-1a-style mix over per-vertex hashes; order-sensitive, which is
        // fine because simplices are canonically sorted before hashing.
        std::size_t h = 1469598103934665603ull;
        std::hash<aleph::types::NodeId> nh{};
        for (const auto& v : s) {
            h ^= nh(v);
            h *= 1099511628211ull;
        }
        return h;
    }
};
