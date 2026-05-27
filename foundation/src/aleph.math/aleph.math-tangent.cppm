export module aleph.math:tangent;

import :types;
import :vec;

export namespace aleph::math {

// Phantom tag types — never instantiated, used only to distinguish
// otherwise-identical TangentVec types at the type level.
struct R3;
struct Surf;

// A tangent vector at a point on a manifold. Wraps a Vec3 but is NOT
// convertible to/from Vec3 implicitly — operations that mix tangent
// frames must go through explicit transforms.
template<typename Manifold>
struct TangentVec {
    Vec3 v{};

    constexpr TangentVec operator+(TangentVec b) const noexcept { return {v + b.v}; }
    constexpr TangentVec operator-(TangentVec b) const noexcept { return {v - b.v}; }
    constexpr TangentVec operator*(f32 s)        const noexcept { return {v * s}; }
};

using TangentR3      = TangentVec<R3>;
using TangentSurface = TangentVec<Surf>;

// Strip the normal component of `dir`, returning the tangent-plane vector.
// `n` must be unit.
[[nodiscard]] constexpr TangentSurface
project_to_tangent(Vec3 dir, Vec3 n) noexcept {
    return TangentSurface{ dir - n * dot(dir, n) };
}

}  // namespace aleph::math
