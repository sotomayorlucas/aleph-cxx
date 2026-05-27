module;
#include <cmath>

export module aleph.math:dual;

import :types;
import :vec;

export namespace aleph::math {

// Forward-mode dual number: x = val + eps·ε, where ε² = 0.
// f(x) = f(val) + f'(val) · eps.
template<typename T>
struct Dual {
    T val{};
    T eps{};
};

// ─── scalar ops ─────────────────────────────────────────────────────
constexpr Dual<f32> operator+(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val + b.val, a.eps + b.eps};
}
constexpr Dual<f32> operator-(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val - b.val, a.eps - b.eps};
}
constexpr Dual<f32> operator*(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val * b.val, a.val * b.eps + a.eps * b.val};
}
constexpr Dual<f32> operator/(Dual<f32> a, Dual<f32> b) noexcept {
    return {a.val / b.val,
            (a.eps * b.val - a.val * b.eps) / (b.val * b.val)};
}

inline Dual<f32> sin(Dual<f32> x) noexcept {
    return {std::sin(x.val), std::cos(x.val) * x.eps};
}
inline Dual<f32> cos(Dual<f32> x) noexcept {
    return {std::cos(x.val), -std::sin(x.val) * x.eps};
}
inline Dual<f32> sqrt(Dual<f32> x) noexcept {
    const f32 v = std::sqrt(x.val);
    return {v, x.eps * (0.5f / v)};
}

// ─── vector ops ─────────────────────────────────────────────────────
constexpr Dual<Vec3> operator+(Dual<Vec3> a, Dual<Vec3> b) noexcept {
    return {a.val + b.val, a.eps + b.eps};
}
constexpr Dual<Vec3> operator-(Dual<Vec3> a, Dual<Vec3> b) noexcept {
    return {a.val - b.val, a.eps - b.eps};
}
constexpr Dual<Vec3> operator*(Dual<Vec3> a, f32 s) noexcept {
    return {a.val * s, a.eps * s};
}

// Dual normalize: d/dt [v/|v|] = (I - n n^T) · dv / |v|.
inline Dual<Vec3> normalize(Dual<Vec3> v) noexcept {
    const f32 inv_len = 1.0f / length(v.val);
    const Vec3 n      = v.val * inv_len;
    const f32  proj   = dot(v.eps, n);
    const Vec3 deps   = (v.eps - n * proj) * inv_len;
    return {n, deps};
}

}  // namespace aleph::math
