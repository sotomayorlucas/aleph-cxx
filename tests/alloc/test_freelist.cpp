#include "doctest.h"
#include <cstring>
#include <cstdint>
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("FreeList returns blocks aligned to request power of two") {
    alignas(64) static unsigned char backing[8192];
    FreeList f{backing, sizeof(backing)};

    void* p8   = f.allocate(8);
    void* p16  = f.allocate(16);
    void* p64  = f.allocate(64);
    void* p128 = f.allocate(128);
    REQUIRE(p8 != nullptr);
    REQUIRE(p16 != nullptr);
    REQUIRE(p64 != nullptr);
    REQUIRE(p128 != nullptr);

    CHECK(reinterpret_cast<std::uintptr_t>(p8)   % 8   == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p16)  % 16  == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p64)  % 64  == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p128) % 64  == 0);

    f.deallocate(p64, 64);
    void* p64b = f.allocate(64);
    CHECK(p64b == p64);   // segregated list recycles
}
