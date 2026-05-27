module;
#include <cmath>

export module aleph.math:bivector;

import :types;
import :vec;

export namespace aleph::math {

// Grade-2 element of G(3,0,0): coefficients on e12, e23, e31.
// A bivector encodes an oriented planar area.
struct Bivector {
    f32 e12{}, e23{}, e31{};
    static constexpr int grade = 2;

    constexpr Bivector operator+(Bivector b) const noexcept {
        return {e12 + b.e12, e23 + b.e23, e31 + b.e31};
    }
    constexpr Bivector operator*(f32 s) const noexcept {
        return {e12 * s, e23 * s, e31 * s};
    }
    constexpr Bivector operator-() const noexcept { return {-e12, -e23, -e31}; }
    constexpr bool operator==(Bivector b) const noexcept {
        return e12 == b.e12 && e23 == b.e23 && e31 == b.e31;
    }
};

// Grade-3 element: the pseudoscalar e123.
struct Trivector {
    f32 e123{};
    static constexpr int grade = 3;
};

// Outer (wedge) product of two grade-1 vectors → bivector.
// e_i ∧ e_j = e_ij, e_i ∧ e_i = 0. Antisymmetric.
[[nodiscard]] constexpr Bivector wedge(Vec3 a, Vec3 b) noexcept {
    return {
        a.x*b.y - a.y*b.x,   // e12
        a.y*b.z - a.z*b.y,   // e23
        a.z*b.x - a.x*b.z,   // e31
    };
}

// Trivolume of three vectors = det.
[[nodiscard]] constexpr Trivector wedge(Vec3 a, Vec3 b, Vec3 c) noexcept {
    return { a.x*(b.y*c.z - b.z*c.y)
           - a.y*(b.x*c.z - b.z*c.x)
           + a.z*(b.x*c.y - b.y*c.x) };
}

}  // namespace aleph::math
