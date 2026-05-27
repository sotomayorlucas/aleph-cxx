#include "doctest.h"
#include <type_traits>
#include <functional>

import aleph.types;

using aleph::types::EdgeId;
using aleph::types::IdAllocator;
using aleph::types::NodeId;

TEST_CASE("NodeId / EdgeId: strong typedefs, not interconvertible") {
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);
    static_assert(!std::is_convertible_v<EdgeId, NodeId>);
    static_assert(!std::is_convertible_v<int, NodeId>);
}

TEST_CASE("NodeId / EdgeId: value, equality, hashability") {
    constexpr NodeId a{7}, b{7}, c{8};
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.value == 7);

    constexpr EdgeId e{42};
    CHECK(e.value == 42);

    std::hash<NodeId> h;
    CHECK(h(a) == h(b));
}

TEST_CASE("IdAllocator: monotonic per-kind allocation from 0") {
    IdAllocator ids;
    CHECK(ids.alloc_node().value == 0);
    CHECK(ids.alloc_node().value == 1);
    CHECK(ids.alloc_edge().value == 0);
    CHECK(ids.alloc_node().value == 2);
    CHECK(ids.alloc_edge().value == 1);
}

TEST_CASE("IdAllocator: copyable as watermark snapshot") {
    IdAllocator a;
    a.alloc_node(); a.alloc_node(); a.alloc_edge();
    IdAllocator b = a;
    CHECK(b.alloc_node().value == 2);
    CHECK(b.alloc_edge().value == 1);
    CHECK(a.alloc_node().value == 2);
    CHECK(a.alloc_edge().value == 1);
}
