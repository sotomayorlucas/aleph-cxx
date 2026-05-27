#include "doctest.h"
import aleph.render.common;
import aleph.alloc;
import aleph.math;

using namespace aleph::render::common;

TEST_CASE("film_alloc backs pixels with an Arena") {
    alignas(64) static unsigned char scratch[64 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    Film f = film_alloc(arena, 16, 16);
    CHECK(f.width == 16);
    CHECK(f.height == 16);
    CHECK(f.stride_pixels == 16);
    REQUIRE(f.pixels != nullptr);
    f.pixels[0] = aleph::math::Vec3{1, 0, 0};
    f.pixels[16] = aleph::math::Vec3{0, 1, 0};
    CHECK(f.pixels[0]  == aleph::math::Vec3{1, 0, 0});
    CHECK(f.pixels[16] == aleph::math::Vec3{0, 1, 0});
}
