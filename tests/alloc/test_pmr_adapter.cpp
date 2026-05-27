#include "doctest.h"
#include <vector>
#include <memory_resource>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Arena works with std::pmr::vector") {
    alignas(16) static unsigned char backing[8192];
    Arena a{backing, sizeof(backing)};

    std::pmr::vector<int> v{&a};
    for (int i = 0; i < 100; ++i) v.push_back(i);
    CHECK(v.size() == 100);
    CHECK(v[42] == 42);
    CHECK(a.bytes_in_use() > 0);
}

TEST_CASE("as_resource returns correct pointer") {
    alignas(16) static unsigned char backing[4096];
    Arena a{backing, sizeof(backing)};

    std::pmr::memory_resource* res = as_resource(a);
    REQUIRE(res != nullptr);
    CHECK(res == &a);

    void* p = res->allocate(64, 16);
    CHECK(p != nullptr);
}

TEST_CASE("pmr_allocator alias works") {
    alignas(16) static unsigned char backing[8192];
    Arena a{backing, sizeof(backing)};

    pmr_allocator<double> alloc(&a);
    double* ptr = alloc.allocate(10);
    REQUIRE(ptr != nullptr);

    ptr[0] = 3.14;
    ptr[9] = 2.71;
    CHECK(ptr[0] == doctest::Approx(3.14));
    CHECK(ptr[9] == doctest::Approx(2.71));

    alloc.deallocate(ptr, 10);
}
