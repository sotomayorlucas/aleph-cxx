#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Vec3 layout: 16-byte aligned, padded w=0") {
    static_assert(sizeof(Vec3)  == 16);
    static_assert(alignof(Vec3) == 16);
    Vec3 v{1, 2, 3};
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.0f);
    CHECK(v.z == 3.0f);
    CHECK(v.w == 0.0f);   // padding always zero
}

TEST_CASE("Vec3 arithmetic") {
    constexpr Vec3 a{1, 2, 3};
    constexpr Vec3 b{4, 5, 6};
    CHECK((a + b) == Vec3{5, 7, 9});
    CHECK((a - b) == Vec3{-3, -3, -3});
    CHECK((a * 2.0f) == Vec3{2, 4, 6});
    CHECK((-a) == Vec3{-1, -2, -3});
    CHECK(dot(a, b) == doctest::Approx(32.0f));
    CHECK(cross(a, b) == Vec3{-3, 6, -3});
    CHECK(length_sq(Vec3{3, 4, 0}) == doctest::Approx(25.0f));
}

TEST_CASE("Vec3 normalize + length") {
    const Vec3 n = normalize(Vec3{3, 4, 0});
    CHECK(length(n) == doctest::Approx(1.0f).epsilon(1e-6f));
    CHECK(n.x == doctest::Approx(0.6f));
    CHECK(n.y == doctest::Approx(0.8f));
}

TEST_CASE("Vec2 basics") {
    static_assert(sizeof(Vec2) == 8);
    constexpr Vec2 a{1, 2}, b{3, 4};
    CHECK((a + b) == Vec2{4, 6});
}

TEST_CASE("Vec4 basics") {
    static_assert(sizeof(Vec4) == 16);
    static_assert(alignof(Vec4) == 16);
    constexpr Vec4 a{1, 2, 3, 4}, b{5, 6, 7, 8};
    CHECK((a + b) == Vec4{6, 8, 10, 12});
}
