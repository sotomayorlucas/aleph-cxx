#include "doctest.h"
#include <type_traits>
import aleph.containers;

using namespace aleph::containers;

struct NodeTag;
struct EdgeTag;

// Wrapper to avoid GCC 16 placement new SFINAE issue
struct IntWrapper {
    int value;
    IntWrapper() : value(0) {}
    IntWrapper(int v) : value(v) {}
};

TEST_CASE("Handle<Tag>: typed integer ID, no implicit conversion") {
    using NodeId = Handle<NodeTag>;
    using EdgeId = Handle<EdgeTag>;
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);
    constexpr NodeId a{42}, b{42}, c{43};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("DenseIndex<NodeTag, IntWrapper>: O(1) lookup, deterministic iteration") {
    DenseIndex<NodeTag, IntWrapper> d;
    IntWrapper w_a(10), w_b(20), w_c(30);
    const auto id_a = d.push(w_a);
    const auto id_b = d.push(w_b);
    const auto id_c = d.push(w_c);

    CHECK(d.size() == 3);
    CHECK(d[id_a].value == 10);
    CHECK(d[id_b].value == 20);
    CHECK(d[id_c].value == 30);

    int sum = 0;
    for (const auto& w : d) sum += w.value;
    CHECK(sum == 60);
}
