module;
#include <span>
#include <algorithm>

export module aleph.math:geom;

import :types;
import :vec;

export namespace aleph::math {

struct alignas(16) Aabb {
    Vec3 min{};
    Vec3 max{};

    constexpr Aabb() = default;
    constexpr Aabb(Vec3 mn, Vec3 mx) noexcept : min(mn), max(mx) {}

    constexpr Aabb translated(Vec3 t) const noexcept { return {min + t, max + t}; }

    static constexpr Aabb from_points(std::span<const Vec3> pts) noexcept {
        Aabb b{pts.front(), pts.front()};
        for (const Vec3& p : pts.subspan(1)) {
            b.min.x = std::min(b.min.x, p.x);
            b.min.y = std::min(b.min.y, p.y);
            b.min.z = std::min(b.min.z, p.z);
            b.max.x = std::max(b.max.x, p.x);
            b.max.y = std::max(b.max.y, p.y);
            b.max.z = std::max(b.max.z, p.z);
        }
        return b;
    }
};

[[nodiscard]] constexpr Aabb union_of(Aabb a, Aabb b) noexcept {
    return {
        Vec3{ std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z) },
        Vec3{ std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z) },
    };
}

struct alignas(16) Ray {
    Vec3 origin{};
    Vec3 dir{};

    constexpr Vec3 at(f32 t) const noexcept { return origin + dir * t; }
};

struct alignas(16) Plane {
    Vec3 normal{};
    f32  d{};

    static constexpr Plane from_point_normal(Vec3 p, Vec3 n) noexcept {
        return { n, -dot(n, p) };
    }
    constexpr f32 distance(Vec3 pt) const noexcept {
        return dot(normal, pt) + d;
    }
};

enum class PlaneSide { Behind = -1, On = 0, Front = 1 };

constexpr PlaneSide classify(Plane pl, Vec3 pt) noexcept {
    const f32 d = pl.distance(pt);
    if (d >  1e-4f) return PlaneSide::Front;
    if (d < -1e-4f) return PlaneSide::Behind;
    return PlaneSide::On;
}

}  // namespace aleph::math
