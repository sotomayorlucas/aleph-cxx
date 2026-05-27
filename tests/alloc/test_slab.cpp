#include "doctest.h"
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Slab<32> allocates and recycles fixed-size blocks") {
    alignas(64) static unsigned char backing[4096];
    Slab<32> s{backing, sizeof(backing)};

    void* a = s.allocate();
    void* b = s.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);

    s.deallocate(a);
    void* c = s.allocate();
    CHECK(c == a);   // free-list reuses last released
}

TEST_CASE("Slab returns nullptr when exhausted") {
    alignas(64) static unsigned char backing[64];
    Slab<32> s{backing, sizeof(backing)};
    CHECK(s.allocate() != nullptr);
    CHECK(s.allocate() != nullptr);
    CHECK(s.allocate() == nullptr);
}
