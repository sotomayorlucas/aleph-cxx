#include "doctest.h"
#include <cmath>
import aleph.math;

using namespace aleph::math;

TEST_CASE("Dual<f32>: identity dual evaluates value + eps") {
    constexpr Dual<f32> x{3.0f, 1.0f};   // x = 3, dx/dx = 1
    CHECK(x.val == 3.0f);
    CHECK(x.eps == 1.0f);
}

TEST_CASE("Dual<f32> + - * / propagate derivatives correctly") {
    const Dual<f32> x{2.0f, 1.0f};
    const Dual<f32> y{3.0f, 0.0f};   // constant

    const Dual<f32> sum  = x + y;
    const Dual<f32> diff = x - y;
    const Dual<f32> prod = x * y;
    const Dual<f32> quot = x / y;

    CHECK(sum.val  == doctest::Approx(5.0f));
    CHECK(sum.eps  == doctest::Approx(1.0f));         // d(x+y)/dx = 1
    CHECK(diff.eps == doctest::Approx(1.0f));         // d(x-y)/dx = 1
    CHECK(prod.eps == doctest::Approx(3.0f));         // d(xy)/dx = y = 3
    CHECK(quot.eps == doctest::Approx(1.0f/3.0f));    // d(x/y)/dx = 1/y
}

TEST_CASE("dual sin: d(sin x)/dx = cos x") {
    const Dual<f32> x{1.0f, 1.0f};
    const Dual<f32> y = sin(x);
    CHECK(y.val == doctest::Approx(std::sin(1.0f)));
    CHECK(y.eps == doctest::Approx(std::cos(1.0f)));
}

TEST_CASE("Dual<Vec3> normalize derivative is unit-circle Jacobian") {
    const Dual<Vec3> v{Vec3{3, 0, 0}, Vec3{1, 0, 0}};
    const Dual<Vec3> n = normalize(v);
    CHECK(n.val == Vec3{1, 0, 0});
    CHECK(approx_eq(n.eps.x, 0.0f, 1e-5f));
    CHECK(approx_eq(n.eps.y, 0.0f, 1e-5f));
    CHECK(approx_eq(n.eps.z, 0.0f, 1e-5f));
}
