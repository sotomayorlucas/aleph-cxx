#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("from_axis_angle: 90° around +y") {
    const Rotor R = from_axis_angle(Vec3{0, 1, 0}, 1.57079632679f);  // π/2
    const Vec3  r = apply(R, Vec3{1, 0, 0});
    CHECK(r.x == doctest::Approx( 0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("slerp endpoints") {
    const Rotor A = from_axis_angle({0, 1, 0}, 0.0f);
    const Rotor B = from_axis_angle({0, 1, 0}, 1.0f);
    const Rotor s0 = slerp(A, B, 0.0f);
    const Rotor s1 = slerp(A, B, 1.0f);
    CHECK(approx_eq(s0.s,   A.s,   1e-5f));
    CHECK(approx_eq(s1.s,   B.s,   1e-5f));
    CHECK(approx_eq(s1.b23, B.b23, 1e-5f));
}

TEST_CASE("to_mat3 of identity is identity matrix") {
    const Mat3 M = to_mat3(Rotor::identity());
    CHECK(M.m[0]  == doctest::Approx(1.0f));
    CHECK(M.m[5]  == doctest::Approx(1.0f));
    CHECK(M.m[10] == doctest::Approx(1.0f));
}

TEST_CASE("from_quat round-trips through to_mat3 vs Quat apply") {
    const f32 half = 0.7071067811f;
    const Quat  q{0.0f, half, 0.0f, half};      // 90° around +y
    const Rotor R = from_quat(q);
    const Vec3  v{1, 0, 0};
    const Vec3  rq = apply(q, v);
    const Vec3  rr = apply(R, v);
    CHECK(rq.x == doctest::Approx(rr.x).epsilon(1e-5f));
    CHECK(rq.y == doctest::Approx(rr.y).epsilon(1e-5f));
    CHECK(rq.z == doctest::Approx(rr.z).epsilon(1e-5f));
}
