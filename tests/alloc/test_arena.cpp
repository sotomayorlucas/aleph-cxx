#include "doctest.h"
#include <cstring>
#include <cstdint>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Arena allocates contiguous bytes") {
    alignas(16) static unsigned char backing[4096];
    Arena a{backing, sizeof(backing)};

    void* p1 = a.allocate(64, 16);
    void* p2 = a.allocate(64, 16);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    CHECK(static_cast<unsigned char*>(p2) - static_cast<unsigned char*>(p1) == 64);
    CHECK(a.bytes_in_use() >= 128);
}

TEST_CASE("Arena alignment respected") {
    alignas(64) static unsigned char backing[4096];
    Arena a{backing, sizeof(backing)};
    void* p = a.allocate(7, 32);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 32 == 0);
}

TEST_CASE("Arena returns nullptr on OOM (no throw)") {
    alignas(16) static unsigned char backing[128];
    Arena a{backing, sizeof(backing)};
    CHECK(a.allocate(64, 16) != nullptr);
    CHECK(a.allocate(64, 16) != nullptr);
    CHECK(a.allocate(64, 16) == nullptr);   // exhausted
}

TEST_CASE("Arena::reset rewinds the bump pointer") {
    alignas(16) static unsigned char backing[1024];
    Arena a{backing, sizeof(backing)};
    (void)a.allocate(256, 16);
    CHECK(a.bytes_in_use() >= 256);
    a.reset();
    CHECK(a.bytes_in_use() == 0);
    CHECK(a.allocate(256, 16) != nullptr);
}
