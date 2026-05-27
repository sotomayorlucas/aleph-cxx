#include "doctest.h"
#include <array>
import aleph.render.sw;
import aleph.math;

using namespace aleph::render::sw;

TEST_CASE("clip_triangle_near: all 3 verts in front → 1 unmodified tri") {
    ClipVert a{0, 0, 0, 1.0f, 0, 0};
    ClipVert b{1, 0, 0, 1.0f, 1, 0};
    ClipVert c{0, 1, 0, 1.0f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(a, b, c, 0.1f, out);
    CHECK(n == 1);
    CHECK(out[0].w == 1.0f);
    CHECK(out[1].w == 1.0f);
    CHECK(out[2].w == 1.0f);
}

TEST_CASE("clip_triangle_near: all 3 behind → 0 tris") {
    ClipVert a{0, 0, 0, 0.05f, 0, 0};
    ClipVert b{1, 0, 0, 0.05f, 1, 0};
    ClipVert c{0, 1, 0, 0.05f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(a, b, c, 0.1f, out);
    CHECK(n == 0);
}

TEST_CASE("clip_triangle_near: 1 in, 2 out → 1 sub-tri") {
    ClipVert in {0, 0, 0, 1.0f,  0, 0};
    ClipVert o1{1, 0, 0, 0.05f, 1, 0};
    ClipVert o2{0, 1, 0, 0.05f, 0, 1};
    std::array<ClipVert, 6> out{};
    const int n = clip_triangle_near(in, o1, o2, 0.1f, out);
    CHECK(n == 1);
    for (int i = 0; i < 3; ++i) CHECK(out[i].w >= 0.1f);
}
