#include "doctest.h"
#include <cstdint>
import aleph.sheaf;
import aleph.types;

using aleph::sheaf::UnionFind;
using aleph::types::NodeId;

// Oracle ported verbatim from aleph-engine/aleph-sheaf/src/union_find.rs
// (test `unions_merge_components`): after union(a,b), a and b share a root and
// c stays disjoint.
TEST_CASE("union_find: unions merge components (Rust oracle)") {
    UnionFind uf;
    const NodeId a{0};
    const NodeId b{1};
    const NodeId c{2};
    uf.make_set(a);
    uf.make_set(b);
    uf.make_set(c);
    uf.union_(a, b);
    CHECK(uf.find(a) == uf.find(b));
    CHECK(uf.find(a) != uf.find(c));
}

// Plan Wave-1 oracle: n singletons -> n components.
TEST_CASE("union_find: singletons yield n components") {
    UnionFind uf;
    for (std::uint32_t i = 0; i < 5; ++i) {
        uf.make_set(NodeId{i});
    }
    CHECK(uf.component_count() == 5);
    // Every singleton is its own root.
    for (std::uint32_t i = 0; i < 5; ++i) {
        CHECK(uf.find(NodeId{i}) == NodeId{i});
    }
}

// Plan Wave-1 oracle: a chain of unions collapses to a single component.
TEST_CASE("union_find: chain of unions collapses to one component") {
    UnionFind uf;
    const std::uint32_t n = 6;
    for (std::uint32_t i = 0; i < n; ++i) {
        uf.make_set(NodeId{i});
    }
    CHECK(uf.component_count() == n);
    for (std::uint32_t i = 0; i + 1 < n; ++i) {
        uf.union_(NodeId{i}, NodeId{i + 1});
    }
    CHECK(uf.component_count() == 1);
    // All members now share one root.
    const NodeId root = uf.find(NodeId{0});
    for (std::uint32_t i = 0; i < n; ++i) {
        CHECK(uf.find(NodeId{i}) == root);
    }
}

// Plan Wave-1 oracle: make_set is idempotent (entry().or_insert semantics) —
// re-inserting an existing node must not split or reset its component.
TEST_CASE("union_find: make_set is idempotent") {
    UnionFind uf;
    const NodeId a{0};
    const NodeId b{1};
    uf.make_set(a);
    uf.make_set(b);
    uf.union_(a, b);
    CHECK(uf.component_count() == 1);
    // Redundant make_set on already-unioned nodes must be a no-op.
    uf.make_set(a);
    uf.make_set(b);
    CHECK(uf.component_count() == 1);
    CHECK(uf.find(a) == uf.find(b));
}

// Union-by-rank determinism: two equal-rank singletons merge, and a third
// disjoint set keeps the count at two components.
TEST_CASE("union_find: two unions of three sets leave two components") {
    UnionFind uf;
    const NodeId a{10};
    const NodeId b{20};
    const NodeId c{30};
    uf.make_set(a);
    uf.make_set(b);
    uf.make_set(c);
    CHECK(uf.component_count() == 3);
    uf.union_(a, b);
    CHECK(uf.component_count() == 2);
    CHECK(uf.find(a) == uf.find(b));
    CHECK(uf.find(c) != uf.find(a));
    // union_ of already-joined roots is a no-op.
    uf.union_(a, b);
    CHECK(uf.component_count() == 2);
}

// Empty union-find reports zero components.
TEST_CASE("union_find: empty has zero components") {
    UnionFind uf;
    CHECK(uf.component_count() == 0);
}
