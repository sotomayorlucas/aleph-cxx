#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Mat4 layout: 64-byte aligned, fits cache line") {
    static_assert(sizeof(Mat4)  == 64);
    static_assert(alignof(Mat4) == 64);
}

TEST_CASE("Mat4 identity * Vec4 = Vec4") {
    constexpr Mat4 I = Mat4::identity();
    const Vec4 v{1, 2, 3, 1};
    const Vec4 r = I * v;
    CHECK(r == v);
}

TEST_CASE("Mat4 translate") {
    const Mat4 T = Mat4::translate({10, 20, 30});
    const Vec4 r = T * Vec4{1, 2, 3, 1};
    CHECK(r == Vec4{11, 22, 33, 1});
}

TEST_CASE("Mat4 scale") {
    const Mat4 S = Mat4::scale({2, 3, 4});
    const Vec4 r = S * Vec4{1, 1, 1, 1};
    CHECK(r == Vec4{2, 3, 4, 1});
}

TEST_CASE("Mat4 multiplication associative on identity") {
    const Mat4 T = Mat4::translate({1, 2, 3});
    const Mat4 I = Mat4::identity();
    const Mat4 R = T * I;
    for (usize i = 0; i < 16; ++i) CHECK(R.m[i] == doctest::Approx(T.m[i]));
}

TEST_CASE("Mat4 look_at gives -Z forward") {
    const Mat4 V = Mat4::look_at({0,0,5}, {0,0,0}, {0,1,0});
    const Vec4 r = V * Vec4{0,0,0,1};
    CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-5.0f).epsilon(1e-5f));
}

TEST_CASE("Mat3 basics") {
    constexpr Mat3 I = Mat3::identity();
    const Vec3 v{1, 2, 3};
    const Vec3 r = I * v;
    CHECK(r == v);
}
