module;
#include <cmath>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

export module aleph.render.common:tonemap;

import aleph.math;

export namespace aleph::render::common {

namespace detail {

// Exact sRGB EOTF (decode) evaluated in double — the single source of truth
// for BOTH lookup tables below, so encode/decode are inverses by construction.
// IEC 61966-2-1 sRGB EOTF (constants 0.04045, 12.92, 0.055, 1.055, 2.4); the
// OETF inverse (0.0031308 split) lives in byte_from_linear's table comment.
inline double srgb_eotf(double e) noexcept {
    return (e <= 0.04045) ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
}

}  // namespace detail

// sRGB EOTF of one byte (texel decode). A 256-entry f32 LUT is EXACT — the
// domain is a byte — and hot-loop cheap (one array read; rast_scan calls this
// 3x per covered pixel). Magic static: thread-safe one-time init, built in
// double then rounded once to f32 => identical bytes across runs. Endpoints
// are exact: 0 -> 0.0f, 255 -> 1.0f.
inline aleph::math::f32 srgb_decode_byte(std::uint8_t b) noexcept {
    static const std::array<aleph::math::f32, 256> lut = [] {
        std::array<aleph::math::f32, 256> t{};
        for (std::size_t i = 0; i < 256; ++i)
            t[i] = static_cast<aleph::math::f32>(
                detail::srgb_eotf(static_cast<double>(i) / 255.0));
        return t;
    }();
    return lut[b];
}

// sRGB OETF (encode) quantized round-to-nearest: round(srgb_oetf(x)·255) with
// oetf(x) = x<=0.0031308 ? 12.92·x : 1.055·x^(1/2.4)−0.055 (IEC 61966-2-1 sRGB
// OETF). A libm pow per
// channel per pixel (~1.4M calls per 800x600 present) is needless present-loop
// cost, so instead: 255 linear THRESHOLDS t[i] = srgb_eotf((i+0.5)/255) — the
// exact decision boundaries of that quantizer (x encodes to byte i iff
// t[i-1] <= x < t[i]) — and an 8-step binary search counting thresholds <= x.
// This matches the ideal quantizer for every f32 except within 1 ulp of a
// boundary, where the double-built f32-rounded threshold decides
// deterministically. Round-trip with srgb_decode_byte is exact for all 256
// bytes (the doctest oracle pins it).
inline std::uint8_t byte_from_linear(aleph::math::f32 x) noexcept {
    static const std::array<aleph::math::f32, 255> thresholds = [] {
        std::array<aleph::math::f32, 255> t{};
        for (std::size_t i = 0; i < 255; ++i)
            t[i] = static_cast<aleph::math::f32>(
                detail::srgb_eotf((static_cast<double>(i) + 0.5) / 255.0));
        return t;
    }();
    // NaN shading inputs clamp to black, not white (std::clamp/upper_bound would
    // otherwise let a NaN fall through to byte 255 silently).
    if (std::isnan(x)) return 0u;
    const aleph::math::f32 clamped = std::clamp(x, 0.0f, 1.0f);
    const auto it =
        std::upper_bound(thresholds.begin(), thresholds.end(), clamped);
    return static_cast<std::uint8_t>(it - thresholds.begin());
}

inline std::uint32_t tonemap_argb8888_srgb(aleph::math::Vec3 linear) noexcept {
    const std::uint32_t a = 0xFFu;
    const std::uint32_t r = byte_from_linear(linear.x);
    const std::uint32_t g = byte_from_linear(linear.y);
    const std::uint32_t b = byte_from_linear(linear.z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

}  // namespace aleph::render::common
