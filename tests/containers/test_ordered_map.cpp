#include "doctest.h"
#include <string>
#include <vector>
import aleph.containers;

using aleph::containers::OrderedMap;

TEST_CASE("OrderedMap: insert returns true once, false on duplicate; size tracks") {
    OrderedMap<int, std::string> m;
    CHECK(m.size() == 0);
    CHECK(m.insert(7, "seven"));
    CHECK(m.size() == 1);
    CHECK(!m.insert(7, "siete"));   // duplicate key
    CHECK(m.size() == 1);
    CHECK(*m.get(7) == "seven");    // value unchanged
}

TEST_CASE("OrderedMap: iteration order matches insertion order") {
    OrderedMap<int, int> m;
    m.insert(3, 30);
    m.insert(1, 10);
    m.insert(4, 40);
    m.insert(1, 99);   // duplicate, ignored
    m.insert(5, 50);

    std::vector<int> keys, vals;
    for (auto [k, v] : m) {
        keys.push_back(k);
        vals.push_back(v);
    }
    CHECK(keys == std::vector<int>{3, 1, 4, 5});
    CHECK(vals == std::vector<int>{30, 10, 40, 50});
}

TEST_CASE("OrderedMap: get returns nullptr on miss, pointer on hit") {
    OrderedMap<int, int> m;
    m.insert(1, 100);
    CHECK(m.get(1) != nullptr);
    CHECK(*m.get(1) == 100);
    CHECK(m.get(2) == nullptr);
}

TEST_CASE("OrderedMap: get_mut allows in-place update without changing order") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    *m.get_mut(2) = 99;

    std::vector<int> vals;
    for (auto [k, v] : m) vals.push_back(v);
    CHECK(vals == std::vector<int>{10, 99, 30});
}

TEST_CASE("OrderedMap: remove returns value and tightens iteration") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    auto v = m.remove(2);
    REQUIRE(v.has_value());
    CHECK(*v == 20);
    CHECK(m.size() == 2);
    CHECK(m.get(2) == nullptr);

    std::vector<int> keys;
    for (auto [k, _] : m) keys.push_back(k);
    CHECK(keys == std::vector<int>{1, 3});
}

TEST_CASE("OrderedMap: re-insert after remove appends at tail") {
    OrderedMap<int, int> m;
    m.insert(1, 10);
    m.insert(2, 20);
    m.insert(3, 30);
    m.remove(1);
    m.insert(1, 11);

    std::vector<int> keys;
    for (auto [k, _] : m) keys.push_back(k);
    CHECK(keys == std::vector<int>{2, 3, 1});
}

TEST_CASE("OrderedMap: clear empties without resetting bucket capacity") {
    OrderedMap<int, int> m;
    for (int i = 0; i < 100; ++i) m.insert(i, i);
    m.clear();
    CHECK(m.size() == 0);
    CHECK(m.get(50) == nullptr);
}

TEST_CASE("OrderedMap: tombstone load triggers rehash before buffer overrun") {
    // 16 buckets default. Insert 11, remove 5, insert 5 more (16 occupied:
    // 11 live + 5 tombstones). Then insert one more — without the
    // tombstones-aware load check, probe would return SIZE_MAX and the
    // next insert would write past the buckets vector.
    OrderedMap<int, int> m;
    for (int i = 0; i < 11; ++i) m.insert(i, i * 10);
    for (int i = 0; i < 5;  ++i) m.remove(i);
    CHECK(m.size() == 6);
    for (int i = 100; i < 105; ++i) m.insert(i, i);
    CHECK(m.size() == 11);
    // Next insert: would crash without the fix
    m.insert(200, 200);
    CHECK(m.size() == 12);
    CHECK(*m.get(200) == 200);
    // And iteration order is still sane (LL is intact)
    std::vector<int> keys;
    for (auto [k, v] : m) keys.push_back(k);
    CHECK(keys.size() == 12);
}
