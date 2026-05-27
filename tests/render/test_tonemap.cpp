#include "doctest.h"
import aleph.render.common;
import aleph.math;

using namespace aleph::render::common;

TEST_CASE("byte_from_linear: 0.0 → 0, 1.0 → 255") {
    CHECK(byte_from_linear(0.0f) == 0u);
    CHECK(byte_from_linear(1.0f) == 255u);
    CHECK(byte_from_linear(0.25f) > 0u);
    CHECK(byte_from_linear(0.25f) < 255u);
}

TEST_CASE("tonemap_argb8888_gamma2: pure red linear -> alpha=255, R=255, G=0, B=0") {
    const auto pixel = tonemap_argb8888_gamma2(aleph::math::Vec3{1.0f, 0.0f, 0.0f});
    CHECK(((pixel >> 24) & 0xFFu) == 0xFFu);
    CHECK(((pixel >> 16) & 0xFFu) == 0xFFu);
    CHECK(((pixel >>  8) & 0xFFu) == 0x00u);
    CHECK((pixel        & 0xFFu) == 0x00u);
}
