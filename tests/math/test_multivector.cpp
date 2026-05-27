#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("Multivector layout: 8 floats, 32-byte aligned") {
    static_assert(sizeof(Multivector)  == 32);
    static_assert(alignof(Multivector) == 32);
}

TEST_CASE("Vec3 * Vec3 = dot scalar + wedge bivector") {
    const Vec3 a{1, 0, 0}, b{0, 1, 0};
    const Multivector m = a * b;
    CHECK(m.s   == doctest::Approx(0.0f));   // a·b = 0
    CHECK(m.e12 == doctest::Approx(1.0f));   // a∧b = e12
    CHECK(m.e23 == doctest::Approx(0.0f));
    CHECK(m.e31 == doctest::Approx(0.0f));
}

TEST_CASE("Rotor::apply rotates Vec3 by 180° around y") {
    const Rotor R{0.0f, 0.0f, 0.0f, 1.0f};   // 180° in x-z
    const Vec3 v{1, 0, 0};
    const Vec3 r = apply(R, v);
    CHECK(r.x == doctest::Approx(-1.0f).epsilon(1e-5f));
    CHECK(r.y == doctest::Approx( 0.0f).epsilon(1e-5f));
    CHECK(r.z == doctest::Approx( 0.0f).epsilon(1e-5f));
}
