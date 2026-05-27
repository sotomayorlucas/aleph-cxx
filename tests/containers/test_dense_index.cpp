#include "doctest.h"
#include <type_traits>
import aleph.containers;

using namespace aleph::containers;

struct NodeTag;
struct EdgeTag;

TEST_CASE("Handle<Tag>: typed integer ID, no implicit conversion") {
    using NodeId = Handle<NodeTag>;
    using EdgeId = Handle<EdgeTag>;
    static_assert(!std::is_convertible_v<NodeId, EdgeId>);
    constexpr NodeId a{42}, b{42}, c{43};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("DenseIndex<NodeTag, int>: O(1) lookup, deterministic iteration") {
    DenseIndex<NodeTag, int> d;
    const auto id_a = d.push(10);
    const auto id_b = d.push(20);
    const auto id_c = d.push(30);

    CHECK(d.size() == 3);
    CHECK(d[id_a] == 10);
    CHECK(d[id_b] == 20);
    CHECK(d[id_c] == 30);

    int sum = 0;
    for (int v : d) sum += v;
    CHECK(sum == 60);
}
