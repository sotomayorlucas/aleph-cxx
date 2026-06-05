#include "doctest.h"
#include <vector>
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
import aleph.alloc;

using namespace aleph::render::sw;

TEST_CASE("rast_scan_textured: writes some pixels for a centered triangle") {
    alignas(64) static unsigned char fbuf[16 * 16 * 16];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film color = aleph::render::common::film_alloc(arena, 16, 16);
    for (int i = 0; i < 16 * 16; ++i) color.pixels[i] = aleph::math::Vec3{1, 0, 1};
    std::vector<aleph::math::f32> depth(16 * 16, 0.0f);
    SpanBuffer sb(16, 16);

    ScreenVert v0{2.0f,  2.0f, 0.0f, 0, 0, 1.0f};
    ScreenVert v1{14.0f, 2.0f, 0.0f, 1, 0, 1.0f};
    ScreenVert v2{8.0f, 14.0f, 0.0f, 0, 1, 1.0f};

    rast_scan_textured(color, depth, sb, v0, v1, v2,
                        tex_floor, nullptr, aleph::math::Vec3{1, 1, 1}, 0, 16);

    int touched = 0;
    for (int i = 0; i < 16 * 16; ++i)
        if (!(color.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    CHECK(touched > 0);
}
