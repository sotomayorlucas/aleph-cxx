#include "doctest.h"
#include <functional>
import aleph.containers;

using namespace aleph::containers;

TEST_CASE("FlatSet inserts unique elements in sorted order") {
    FlatSet<int> s;
    s.insert(5);
    s.insert(2);
    s.insert(8);
    s.insert(2);   // dup
    CHECK(s.size() == 3);
    CHECK(s[0] == 2);
    CHECK(s[1] == 5);
    CHECK(s[2] == 8);
}

TEST_CASE("FlatSet::contains uses binary search") {
    FlatSet<int> s;
    for (int i = 0; i < 100; ++i) s.insert(i * 2);
    CHECK(s.contains(0));
    CHECK(s.contains(50));
    CHECK_FALSE(s.contains(51));
}
