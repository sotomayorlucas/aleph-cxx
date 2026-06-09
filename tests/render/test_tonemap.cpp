#include "doctest.h"
#include <cstdint>
import aleph.render.common;
import aleph.math;

using namespace aleph::render::common;

TEST_CASE("byte_from_linear: 0.0 → 0, 1.0 → 255") {
    CHECK(byte_from_linear(0.0f) == 0u);
    CHECK(byte_from_linear(1.0f) == 255u);
    CHECK(byte_from_linear(0.25f) > 0u);
    CHECK(byte_from_linear(0.25f) < 255u);
}

// THE key sRGB oracle: the OETF (encode) and EOTF (decode) are exact inverses
// at every representable byte. Any drift in either table breaks this.
TEST_CASE("sRGB transfer pair: byte_from_linear(srgb_decode_byte(b)) == b for all 256 bytes") {
    for (int b = 0; b < 256; ++b) {
        const aleph::math::f32 lin = srgb_decode_byte(static_cast<std::uint8_t>(b));
        CHECK(byte_from_linear(lin) == static_cast<std::uint8_t>(b));
    }
}

TEST_CASE("sRGB OETF: pins at the linear-segment boundary and curve shape") {
    // At x = 0.0031308 both sRGB segments meet: oetf = 12.92*x = 0.04045,
    // and 0.04045*255 = 10.31… rounds to byte 10.
    CHECK(byte_from_linear(0.0031308f) == 10u);
    // Monotone non-decreasing across the boundary.
    CHECK(byte_from_linear(0.0031f) <= byte_from_linear(0.0032f));
    // sRGB encode of mid-grey 0.2158605 (decode of byte 128) — NOT gamma-2.0:
    // sqrt(0.2158605)*255 ≈ 118, sRGB gives exactly 128.
    CHECK(byte_from_linear(0.215860501f) == 128u);
}

TEST_CASE("srgb_decode_byte: endpoints exact, strictly increasing") {
    CHECK(srgb_decode_byte(0) == 0.0f);
    CHECK(srgb_decode_byte(255) == 1.0f);
    for (int b = 1; b < 256; ++b)
        CHECK(srgb_decode_byte(static_cast<std::uint8_t>(b))
              > srgb_decode_byte(static_cast<std::uint8_t>(b - 1)));
}

TEST_CASE("tonemap_argb8888_srgb: pure red linear -> alpha=255, R=255, G=0, B=0") {
    const auto pixel = tonemap_argb8888_srgb(aleph::math::Vec3{1.0f, 0.0f, 0.0f});
    CHECK(((pixel >> 24) & 0xFFu) == 0xFFu);
    CHECK(((pixel >> 16) & 0xFFu) == 0xFFu);
    CHECK(((pixel >>  8) & 0xFFu) == 0x00u);
    CHECK((pixel        & 0xFFu) == 0x00u);
}
