module;
#include <type_traits>
#include <cmath>

export module aleph.math:rotor;

import :types;
import :vec;
import :mat;
import :quat;

export namespace aleph::math {

// G(3,0,0) even-grade element: scalar + bivector.
// Components in basis {1, e12, e23, e31}.
// Hot path: compose is 8 mul + 7 add. AVX2-friendly when batched.
struct alignas(16) Rotor {
    f32 s{1.0f};
    f32 b12{}, b23{}, b31{};

    static constexpr int grade = 0;   // even-grade: 0 + 2

    static constexpr Rotor identity() noexcept { return Rotor{1, 0, 0, 0}; }
};

// Geometric product of two rotors. Derived from the multiplication table
// of the even subalgebra of G(3,0,0). 8 mul + 7 add.
[[nodiscard]] constexpr Rotor operator*(Rotor a, Rotor b) noexcept {
    return {
        a.s*b.s   - a.b12*b.b12 - a.b23*b.b23 - a.b31*b.b31,
        a.s*b.b12 + a.b12*b.s   + a.b23*b.b31 - a.b31*b.b23,
        a.s*b.b23 - a.b12*b.b31 + a.b23*b.s   + a.b31*b.b12,
        a.s*b.b31 + a.b12*b.b23 - a.b23*b.b12 + a.b31*b.s,
    };
}

[[nodiscard]] constexpr Rotor reverse(Rotor R) noexcept {
    return Rotor{R.s, -R.b12, -R.b23, -R.b31};
}

// Rotate v by R: v' = R v R⁻¹. Closed-form expansion of the sandwich
// for grade-1 input, avoiding the full multivector roundtrip.
[[nodiscard]] inline Vec3 apply(Rotor R, Vec3 v) noexcept {
    const f32 sx  = R.s;
    const f32 bxy = R.b12, byz = R.b23, bzx = R.b31;
    const f32 t_x   =  sx*v.x + bxy*v.y - bzx*v.z;
    const f32 t_y   =  sx*v.y - bxy*v.x + byz*v.z;
    const f32 t_z   =  sx*v.z - byz*v.y + bzx*v.x;
    const f32 t_xyz = bxy*v.z + byz*v.x + bzx*v.y;
    return Vec3{
         t_x*sx + t_y*bxy - t_z*bzx + t_xyz*byz,
         t_y*sx - t_x*bxy + t_z*byz + t_xyz*bzx,
         t_z*sx + t_x*bzx - t_y*byz + t_xyz*bxy,
    };
}

// Build a unit rotor that rotates by `rad` radians around the unit axis `axis`.
// Convention: R = cos(θ/2) − sin(θ/2)·B, where B = x·e23 + y·e31 + z·e12
// is the unit bivector dual to `axis`.  The minus sign gives the standard
// right-hand active rotation (90° around +y maps +x → −z).
[[nodiscard]] inline Rotor from_axis_angle(Vec3 axis, f32 rad) noexcept {
    const f32 half = 0.5f * rad;
    const f32 c = std::cos(half);
    const f32 s = std::sin(half);
    return Rotor{ c, -s * axis.z, -s * axis.x, -s * axis.y };
}

// Spherical linear interpolation between two unit rotors.
[[nodiscard]] inline Rotor slerp(Rotor a, Rotor b, f32 t) noexcept {
    f32 d = a.s*b.s + a.b12*b.b12 + a.b23*b.b23 + a.b31*b.b31;
    Rotor bb = b;
    if (d < 0.0f) {
        bb = Rotor{-b.s, -b.b12, -b.b23, -b.b31};
        d = -d;
    }
    if (d > 0.9995f) {
        Rotor r{
            a.s   + t * (bb.s   - a.s),
            a.b12 + t * (bb.b12 - a.b12),
            a.b23 + t * (bb.b23 - a.b23),
            a.b31 + t * (bb.b31 - a.b31),
        };
        const f32 inv = 1.0f / std::sqrt(r.s*r.s + r.b12*r.b12 + r.b23*r.b23 + r.b31*r.b31);
        return {r.s*inv, r.b12*inv, r.b23*inv, r.b31*inv};
    }
    const f32 theta_0 = std::acos(d);
    const f32 sin_0   = std::sin(theta_0);
    const f32 theta   = theta_0 * t;
    const f32 wa      = std::sin(theta_0 - theta) / sin_0;
    const f32 wb      = std::sin(theta)           / sin_0;
    return Rotor{
        wa*a.s   + wb*bb.s,
        wa*a.b12 + wb*bb.b12,
        wa*a.b23 + wb*bb.b23,
        wa*a.b31 + wb*bb.b31,
    };
}

// Convert a unit rotor to its equivalent column-major 3×3 rotation matrix.
// Derived by expanding apply(R, eᵢ) for each basis vector to fill each column.
// m[col*4 + row] layout (stride-4, last slot of each column is padding).
[[nodiscard]] inline Mat3 to_mat3(Rotor R) noexcept {
    const f32 s   = R.s;
    const f32 b12 = R.b12, b23 = R.b23, b31 = R.b31;
    Mat3 m{};
    // Col 0 = apply(R, e1)
    m.m[0]  = 1.0f - 2.0f*(b12*b12 + b31*b31);
    m.m[1]  =        2.0f*(b23*b31 - s*b12);
    m.m[2]  =        2.0f*(s*b31   + b12*b23);
    // Col 1 = apply(R, e2)
    m.m[4]  =        2.0f*(s*b12   + b23*b31);
    m.m[5]  = 1.0f - 2.0f*(b12*b12 + b23*b23);
    m.m[6]  =        2.0f*(b12*b31 - s*b23);
    // Col 2 = apply(R, e3)
    m.m[8]  =        2.0f*(b12*b23 - s*b31);
    m.m[9]  =        2.0f*(s*b23   + b12*b31);
    m.m[10] = 1.0f - 2.0f*(b23*b23 + b31*b31);
    return m;
}

// Quaternion (x,y,z,w) → Rotor with the same rotation.
// In Cl(3,0,0): i = −e23, j = −e31, k = −e12, so the bivector signs flip.
[[nodiscard]] constexpr Rotor from_quat(Quat q) noexcept {
    return Rotor{ q.w, -q.z, -q.x, -q.y };
}

}  // namespace aleph::math
