#include "doctest.h"
#include <type_traits>
import aleph.math;

using namespace aleph::math;

TEST_CASE("TangentR3 and TangentSurface are distinct types") {
    static_assert(!std::is_same_v<TangentR3, TangentSurface>);
    static_assert(!std::is_same_v<TangentR3, Vec3>);
}

TEST_CASE("TangentR3 wraps Vec3, exposes .v") {
    const TangentR3 t{Vec3{1, 2, 3}};
    CHECK(t.v == Vec3{1, 2, 3});
}

TEST_CASE("project_to_tangent removes normal component") {
    const Vec3 n{0, 1, 0};
    const Vec3 dir{1, 1, 0};
    const TangentSurface t = project_to_tangent(dir, n);
    CHECK(approx_eq(t.v.y, 0.0f, 1e-6f));
    CHECK(t.v.x == doctest::Approx(1.0f));
    CHECK(t.v.z == doctest::Approx(0.0f));
}
