module;
#include <type_traits>

export module aleph.math:multivector;

import :types;
import :vec;
import :bivector;

export namespace aleph::math {

// Full dense G(3,0,0) element: {1, e1, e2, e3, e12, e23, e31, e123}.
// 32-byte aligned so 1 multivector = 1 __m256 (AVX2 lane).
struct alignas(32) Multivector {
    f32 s{};
    f32 e1{}, e2{}, e3{};
    f32 e12{}, e23{}, e31{};
    f32 e123{};
};

// Geometric product of two grade-1 vectors: a * b = a·b + a∧b.
[[nodiscard]] constexpr Multivector operator*(Vec3 a, Vec3 b) noexcept {
    Multivector m{};
    m.s   = a.x*b.x + a.y*b.y + a.z*b.z;
    m.e12 = a.x*b.y - a.y*b.x;
    m.e23 = a.y*b.z - a.z*b.y;
    m.e31 = a.z*b.x - a.x*b.z;
    return m;
}

}  // namespace aleph::math
