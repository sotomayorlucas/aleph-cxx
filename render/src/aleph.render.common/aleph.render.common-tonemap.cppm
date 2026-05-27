module;
#include <cmath>
#include <algorithm>
#include <cstdint>

export module aleph.render.common:tonemap;

import aleph.math;

export namespace aleph::render::common {

inline std::uint8_t byte_from_linear(aleph::math::f32 x) noexcept {
    const aleph::math::f32 clamped = std::clamp(x, 0.0f, 1.0f);
    const aleph::math::f32 g = std::sqrt(clamped);
    return static_cast<std::uint8_t>(255.999f * g);
}

inline std::uint32_t tonemap_argb8888_gamma2(aleph::math::Vec3 linear) noexcept {
    const std::uint32_t a = 0xFFu;
    const std::uint32_t r = byte_from_linear(linear.x);
    const std::uint32_t g = byte_from_linear(linear.y);
    const std::uint32_t b = byte_from_linear(linear.z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

}  // namespace aleph::render::common
