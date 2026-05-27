#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Bivector layout: 12 bytes, natural alignment") {
    static_assert(sizeof(Bivector) == 12);
    static_assert(Bivector::grade == 2);
}

TEST_CASE("Trivector layout: 4 bytes, pseudoscalar") {
    static_assert(sizeof(Trivector) == 4);
    static_assert(Trivector::grade == 3);
}

TEST_CASE("wedge of parallel vectors is zero") {
    const Bivector b = wedge(Vec3{1, 0, 0}, Vec3{2, 0, 0});
    CHECK(approx_eq(b.e12, 0.0f));
    CHECK(approx_eq(b.e23, 0.0f));
    CHECK(approx_eq(b.e31, 0.0f));
}

TEST_CASE("wedge of x and y gives e12 = 1") {
    const Bivector b = wedge(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    CHECK(b.e12 == doctest::Approx(1.0f));
    CHECK(b.e23 == doctest::Approx(0.0f));
    CHECK(b.e31 == doctest::Approx(0.0f));
}

TEST_CASE("wedge is antisymmetric") {
    const Vec3 a{1, 2, 3}, b{4, 5, 6};
    const Bivector ab = wedge(a, b);
    const Bivector ba = wedge(b, a);
    CHECK(approx_eq(ab.e12, -ba.e12));
    CHECK(approx_eq(ab.e23, -ba.e23));
    CHECK(approx_eq(ab.e31, -ba.e31));
}

TEST_CASE("trivector wedge") {
    const Trivector t = wedge(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1});
    CHECK(t.e123 == doctest::Approx(1.0f));
}

TEST_CASE("trivector wedge determinant") {
    const Trivector t = wedge(Vec3{1, 2, 3}, Vec3{4, 5, 6}, Vec3{7, 8, 10});
    CHECK(t.e123 == doctest::Approx(-3.0f));
}

TEST_CASE("Bivector arithmetic") {
    constexpr Bivector b1{1, 2, 3};
    constexpr Bivector b2{4, 5, 6};
    CHECK((b1 + b2) == Bivector{5, 7, 9});
    CHECK((b1 * 2.0f) == Bivector{2, 4, 6});
    CHECK((-b1) == Bivector{-1, -2, -3});
}
