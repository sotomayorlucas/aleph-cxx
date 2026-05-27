#include "doctest.h"
import aleph.alloc;

using namespace aleph::alloc;

TEST_CASE("Frame: write to A, release, write to B, release recycles A") {
    alignas(16) static unsigned char backing_a[1024];
    alignas(16) static unsigned char backing_b[1024];
    Frame f{backing_a, backing_b, 1024};

    void* p1 = f.allocate(256, 16);
    CHECK(p1 != nullptr);
    CHECK(f.bytes_in_use_current() == 256);

    f.release_frame();   // flip to backing_b
    CHECK(f.bytes_in_use_current() == 0);

    void* p2 = f.allocate(512, 16);
    CHECK(p2 != nullptr);
    CHECK(p2 != p1);                  // different buffer

    f.release_frame();   // flip back to backing_a
    void* p3 = f.allocate(64, 16);
    CHECK(p3 == p1);                  // recycled the first buffer's start
}
