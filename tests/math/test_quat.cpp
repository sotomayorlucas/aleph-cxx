#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Quat layout: 16 bytes, alignas 16") {
    static_assert(sizeof(Quat)  == 16);
    static_assert(alignof(Quat) == 16);
}

TEST_CASE("Quat identity") {
    constexpr Quat q = Quat::identity();
    CHECK(q.x == 0.0f); CHECK(q.y == 0.0f); CHECK(q.z == 0.0f); CHECK(q.w == 1.0f);
}

TEST_CASE("Quat compose: identity * q = q") {
    const Quat a = Quat::identity();
    const Quat b{0.1f, 0.2f, 0.3f, 0.9273f};
    const Quat c = a * b;
    CHECK(c.x == doctest::Approx(b.x));
    CHECK(c.y == doctest::Approx(b.y));
    CHECK(c.z == doctest::Approx(b.z));
    CHECK(c.w == doctest::Approx(b.w));
}

TEST_CASE("Quat rotate a vector") {
    const f32 half = 0.7071067811f;
    const Quat q{0.0f, half, 0.0f, half};
    const Vec3 r = apply(q, Vec3{1, 0, 0});
    CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx(-1.0f).epsilon(1e-5f));
}
