module;
#include <cmath>
#include <immintrin.h>

export module aleph.math:vec;

import :types;

export namespace aleph::math {

struct Vec2 {
    f32 x{}, y{};
    using scalar_type = f32;

    constexpr Vec2 operator+(Vec2 b) const noexcept { return {x + b.x, y + b.y}; }
    constexpr Vec2 operator-(Vec2 b) const noexcept { return {x - b.x, y - b.y}; }
    constexpr Vec2 operator*(f32 s)  const noexcept { return {x * s,   y * s}; }
    constexpr Vec2 operator-()       const noexcept { return {-x, -y}; }
    constexpr bool operator==(Vec2 o) const noexcept { return x == o.x && y == o.y; }
};

// 16-byte aligned, w padded to 0. 1 Vec3 == 1 __m128 — single-vector
// ops fold to one SIMD insn each.
struct alignas(16) Vec3 {
    f32 x{}, y{}, z{}, w{};
    using scalar_type = f32;
    static constexpr int grade = 1;

    constexpr Vec3() = default;
    constexpr Vec3(f32 X, f32 Y, f32 Z) noexcept : x{X}, y{Y}, z{Z}, w{0} {}

    constexpr Vec3 operator+(Vec3 b) const noexcept { return {x + b.x, y + b.y, z + b.z}; }
    constexpr Vec3 operator-(Vec3 b) const noexcept { return {x - b.x, y - b.y, z - b.z}; }
    constexpr Vec3 operator*(f32 s)  const noexcept { return {x * s,   y * s,   z * s}; }
    constexpr Vec3 operator/(f32 s)  const noexcept { return {x / s,   y / s,   z / s}; }
    constexpr Vec3 operator-()       const noexcept { return {-x, -y, -z}; }
    constexpr Vec3& operator+=(Vec3 b) noexcept { x += b.x; y += b.y; z += b.z; return *this; }
    constexpr Vec3& operator-=(Vec3 b) noexcept { x -= b.x; y -= b.y; z -= b.z; return *this; }
    constexpr Vec3& operator*=(f32 s)  noexcept { x *= s;   y *= s;   z *= s;   return *this; }
    constexpr bool  operator==(Vec3 o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct alignas(16) Vec4 {
    f32 x{}, y{}, z{}, w{};
    using scalar_type = f32;

    constexpr Vec4 operator+(Vec4 b) const noexcept {
        return {x + b.x, y + b.y, z + b.z, w + b.w};
    }
    constexpr Vec4 operator-(Vec4 b) const noexcept {
        return {x - b.x, y - b.y, z - b.z, w - b.w};
    }
    constexpr Vec4 operator*(f32 s) const noexcept {
        return {x * s, y * s, z * s, w * s};
    }
    constexpr bool operator==(Vec4 o) const noexcept {
        return x == o.x && y == o.y && z == o.z && w == o.w;
    }
};

[[nodiscard]] constexpr f32 dot(Vec3 a, Vec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}
[[nodiscard]] constexpr f32 length_sq(Vec3 a) noexcept { return dot(a, a); }

[[nodiscard]] inline f32 length(Vec3 a) noexcept { return std::sqrt(length_sq(a)); }

[[nodiscard]] inline Vec3 normalize(Vec3 a) noexcept {
    return a * (1.0f / length(a));
}

[[nodiscard]] constexpr Vec3 lerp(Vec3 a, Vec3 b, f32 t) noexcept {
    return a * (1.0f - t) + b * t;
}

[[nodiscard]] constexpr Vec3 reflect(Vec3 v, Vec3 n) noexcept {
    return v - n * (2.0f * dot(v, n));
}

// Snell's law refraction. etaI_over_etaT = eta_i / eta_t.
// Assumes uv is a unit incident vector, n is unit normal facing incident side.
[[nodiscard]] inline Vec3 refract(Vec3 uv, Vec3 n, f32 etaI_over_etaT) noexcept {
    const f32 cos_theta = dot(-uv, n);
    const Vec3 r_out_perp = (uv + n * cos_theta) * etaI_over_etaT;
    const f32 k = 1.0f - dot(r_out_perp, r_out_perp);
    const Vec3 r_out_para = n * -std::sqrt(k < 0.0f ? 0.0f : k);
    return r_out_perp + r_out_para;
}

[[nodiscard]] constexpr bool near_zero(Vec3 v) noexcept {
    constexpr f32 eps = 1e-8f;
    auto abs_f = [](f32 x) constexpr { return x < 0 ? -x : x; };
    return abs_f(v.x) < eps && abs_f(v.y) < eps && abs_f(v.z) < eps;
}

// Component-wise (Hadamard) product. Named to avoid collision with the
// geometric product operator*(Vec3,Vec3)->Multivector in :multivector.
[[nodiscard]] constexpr Vec3 hadamard(Vec3 a, Vec3 b) noexcept {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

}  // namespace aleph::math
