// Union-find over NodeId. Path compression + union-by-rank.
//
// Faithful port of aleph-engine/aleph-sheaf/src/union_find.rs.
// Rust backs `parent`/`rank` with `IndexMap` (insertion-ordered) so roots are
// deterministic; here we use the move-only `aleph::containers::OrderedMap`,
// which preserves insertion order identically. `union` is renamed `union_`
// (reserved word in C++). `component_count` mirrors the distinct-root bucket
// pass in cohomology.rs (`compute_h0`).
module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

export module aleph.sheaf:union_find;

import aleph.types;
import aleph.containers;

export namespace aleph::sheaf {

class UnionFind {
public:
    using NodeId = aleph::types::NodeId;

    UnionFind() = default;

    // Rust: parent.entry(x).or_insert(x); rank.entry(x).or_insert(0).
    // OrderedMap::insert is a no-op when the key already exists, giving the
    // same idempotent "only insert if absent" semantics.
    void make_set(NodeId x) {
        parent_.insert(x, x);
        rank_.insert(x, std::uint32_t{0});
    }

    // Walk to the root, then path-compress by re-pointing every node on the
    // walk directly at the root. Absent keys are a programmer error (the Rust
    // reference indexes with `self.parent[&current]`, which panics) — assert.
    NodeId find(NodeId x) {
        NodeId current = x;
        for (;;) {
            const NodeId* p = parent_.get(current);
            assert(p != nullptr && "find: node was never make_set");
            if (*p == current) {
                break;
            }
            current = *p;
        }
        // Path compression: redo the walk and point everyone to the root.
        const NodeId root   = current;
        NodeId       walker = x;
        for (;;) {
            NodeId* pw = parent_.get_mut(walker);
            assert(pw != nullptr && "find: node was never make_set");
            if (*pw == root) {
                break;
            }
            const NodeId next = *pw;
            *pw = root;  // IndexMap::insert overwrites; mirror via get_mut.
            walker = next;
        }
        return root;
    }

    void union_(NodeId a, NodeId b) {
        const NodeId ra = find(a);
        const NodeId rb = find(b);
        if (ra == rb) {
            return;
        }
        const std::uint32_t* rank_ra = rank_.get(ra);
        const std::uint32_t* rank_rb = rank_.get(rb);
        assert(rank_ra != nullptr && rank_rb != nullptr &&
               "union_: roots must have ranks");

        NodeId small;
        NodeId large;
        if (*rank_ra < *rank_rb) {
            small = ra;
            large = rb;
        } else {
            small = rb;
            large = ra;
        }
        // parent.insert(small, large): small already present, so overwrite via
        // get_mut (OrderedMap::insert would no-op on the existing key).
        NodeId* ps = parent_.get_mut(small);
        assert(ps != nullptr);
        *ps = large;

        if (*rank_ra == *rank_rb) {
            std::uint32_t* rl = rank_.get_mut(large);
            assert(rl != nullptr);
            *rl += 1;
        }
    }

    // Number of disjoint components: distinct roots across all make_set nodes.
    // Mirrors the bucket-by-root pass in cohomology.rs `compute_h0`.
    std::size_t component_count() {
        // Snapshot keys first: find() path-compresses (mutates parent_), so we
        // must not iterate the map while compressing it.
        std::vector<NodeId> keys;
        keys.reserve(parent_.size());
        for (auto [k, v] : parent_) {
            (void)v;
            keys.push_back(k);
        }
        std::vector<NodeId> roots;
        for (const NodeId k : keys) {
            const NodeId r = find(k);
            bool seen = false;
            for (const NodeId existing : roots) {
                if (existing == r) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                roots.push_back(r);
            }
        }
        return roots.size();
    }

private:
    aleph::containers::OrderedMap<NodeId, NodeId>        parent_;
    aleph::containers::OrderedMap<NodeId, std::uint32_t> rank_;
};

}  // namespace aleph::sheaf
