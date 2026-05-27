module;
#include <cmath>

export module aleph.math:quat;

import :types;
import :vec;

export namespace aleph::math {

// LEGACY rotation type. Use Rotor for new code (task 8+).
// Layout: imaginary first (x, y, z), real last (w) — matches most assets.
struct alignas(16) Quat {
    f32 x{}, y{}, z{}, w{1.0f};

    static constexpr Quat identity() noexcept { return Quat{0, 0, 0, 1}; }
};

// Hamilton product: a * b (apply b first, then a).
[[nodiscard]] constexpr Quat operator*(Quat a, Quat b) noexcept {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
    };
}

// Rotate v by q: v' = q v q⁻¹. Assumes q is unit.
[[nodiscard]] inline Vec3 apply(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

}  // namespace aleph::math
