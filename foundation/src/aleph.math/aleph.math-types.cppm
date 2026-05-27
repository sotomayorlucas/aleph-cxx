module;
#include <cstdint>
#include <cstddef>
#include <cmath>

export module aleph.math:types;

export namespace aleph::math {

using u8    = std::uint8_t;
using u16   = std::uint16_t;
using u32   = std::uint32_t;
using u64   = std::uint64_t;
using i8    = std::int8_t;
using i16   = std::int16_t;
using i32   = std::int32_t;
using i64   = std::int64_t;
using f32   = float;
using f64   = double;
using usize = std::size_t;

inline constexpr f32 pi_f     = 3.14159265358979323846f;
inline constexpr f32 two_pi_f = 6.28318530717958647692f;

[[nodiscard]] constexpr f32 deg_to_rad(f32 deg) noexcept {
    return deg * (pi_f / 180.0f);
}

[[nodiscard]] constexpr bool approx_eq(f32 a, f32 b, f32 eps = 1e-6f) noexcept {
    const f32 d = a - b;
    return (d < 0.0f ? -d : d) <= eps;
}

[[nodiscard]] constexpr bool approx_eq(f64 a, f64 b, f64 eps = 1e-12) noexcept {
    const f64 d = a - b;
    return (d < 0.0 ? -d : d) <= eps;
}

}  // namespace aleph::math
