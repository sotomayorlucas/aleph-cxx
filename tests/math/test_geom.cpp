#include "doctest.h"
#include <array>
import aleph.math;

using namespace aleph::math;

TEST_CASE("Aabb basic construction + union") {
    constexpr Aabb a{{-1, -1, -1}, {1, 1, 1}};
    constexpr Aabb b{{ 0,  0,  0}, {2, 2, 2}};
    constexpr Aabb u = union_of(a, b);
    CHECK(u.min == Vec3{-1, -1, -1});
    CHECK(u.max == Vec3{ 2,  2,  2});
}

TEST_CASE("Aabb from_points") {
    const std::array<Vec3, 3> pts{ Vec3{1,0,0}, Vec3{0,2,0}, Vec3{-1,1,3} };
    const Aabb b = Aabb::from_points(pts);
    CHECK(b.min == Vec3{-1, 0, 0});
    CHECK(b.max == Vec3{ 1, 2, 3});
}

TEST_CASE("Ray::at evaluates O + t·D") {
    constexpr Ray r{{0, 0, 0}, {1, 0, 0}};
    CHECK(r.at(2.5f) == Vec3{2.5f, 0, 0});
}

TEST_CASE("Plane: distance + classify") {
    const Plane p = Plane::from_point_normal({0, 0, 0}, {0, 1, 0});
    CHECK(p.distance({1, 3, 5})  == doctest::Approx( 3.0f));
    CHECK(p.distance({1, -2, 5}) == doctest::Approx(-2.0f));
    CHECK(classify(p, Vec3{0, 1, 0})   == PlaneSide::Front);
    CHECK(classify(p, Vec3{0, -1, 0})  == PlaneSide::Behind);
    CHECK(classify(p, Vec3{0, 0, 0})   == PlaneSide::On);
}
