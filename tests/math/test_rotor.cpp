#include "doctest.h"
#include <type_traits>
import aleph.math;

using namespace aleph::math;

TEST_CASE("Rotor layout: 16 bytes, alignas 16, trivially copyable") {
    static_assert(sizeof(Rotor)  == 16);
    static_assert(alignof(Rotor) == 16);
    static_assert(std::is_trivially_copyable_v<Rotor>);
}

TEST_CASE("Rotor identity") {
    constexpr Rotor R = Rotor::identity();
    CHECK(R.s   == 1.0f);
    CHECK(R.b12 == 0.0f);
    CHECK(R.b23 == 0.0f);
    CHECK(R.b31 == 0.0f);
}

TEST_CASE("Rotor compose: identity is neutral") {
    const Rotor I = Rotor::identity();
    const Rotor A{0.9f, 0.3f, 0.1f, 0.2f};
    const Rotor IA = I * A;
    const Rotor AI = A * I;
    CHECK(IA.s   == doctest::Approx(A.s));
    CHECK(IA.b12 == doctest::Approx(A.b12));
    CHECK(AI.s   == doctest::Approx(A.s));
    CHECK(AI.b23 == doctest::Approx(A.b23));
}

TEST_CASE("Rotor compose is associative") {
    const Rotor A{0.7f, 0.5f, 0.4f, 0.3f};
    const Rotor B{0.6f, 0.2f, 0.5f, 0.6f};
    const Rotor C{0.8f, 0.3f, 0.1f, 0.5f};
    const Rotor lhs = A * (B * C);
    const Rotor rhs = (A * B) * C;
    CHECK(approx_eq(lhs.s,   rhs.s,   1e-5f));
    CHECK(approx_eq(lhs.b12, rhs.b12, 1e-5f));
    CHECK(approx_eq(lhs.b23, rhs.b23, 1e-5f));
    CHECK(approx_eq(lhs.b31, rhs.b31, 1e-5f));
}
