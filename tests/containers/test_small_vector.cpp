#include "doctest.h"
#include <utility>
import aleph.containers;

using namespace aleph::containers;

TEST_CASE("SmallVector<int, 4>: inline up to N, no heap allocations") {
    SmallVector<int, 4> v;
    for (int i = 0; i < 4; ++i) v.push_back(i);
    CHECK(v.size() == 4);
    CHECK(v.is_inline());
    for (int i = 0; i < 4; ++i) CHECK(v[i] == i);
}

TEST_CASE("SmallVector spills to heap past N") {
    SmallVector<int, 4> v;
    for (int i = 0; i < 10; ++i) v.push_back(i);
    CHECK(v.size() == 10);
    CHECK_FALSE(v.is_inline());
    for (int i = 0; i < 10; ++i) CHECK(v[i] == i);
}

TEST_CASE("SmallVector noexcept move + range-based for") {
    SmallVector<int, 4> a;
    for (int i = 0; i < 6; ++i) a.push_back(i);
    SmallVector<int, 4> b = std::move(a);
    int sum = 0;
    for (int x : b) sum += x;
    CHECK(sum == 15);
}
