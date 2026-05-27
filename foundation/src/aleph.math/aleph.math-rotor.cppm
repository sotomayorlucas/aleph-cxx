module;
#include <type_traits>

export module aleph.math:rotor;

import :types;

export namespace aleph::math {

// G(3,0,0) even-grade element: scalar + bivector.
// Components in basis {1, e12, e23, e31}.
// Hot path: compose is 8 mul + 7 add. AVX2-friendly when batched.
struct alignas(16) Rotor {
    f32 s{1.0f};
    f32 b12{}, b23{}, b31{};

    static constexpr int grade = 0;   // even-grade: 0 + 2

    static constexpr Rotor identity() noexcept { return Rotor{1, 0, 0, 0}; }
};

// Geometric product of two rotors. Derived from the multiplication table
// of the even subalgebra of G(3,0,0). 8 mul + 7 add.
[[nodiscard]] constexpr Rotor operator*(Rotor a, Rotor b) noexcept {
    return {
        a.s*b.s   - a.b12*b.b12 - a.b23*b.b23 - a.b31*b.b31,
        a.s*b.b12 + a.b12*b.s   + a.b23*b.b31 - a.b31*b.b23,
        a.s*b.b23 - a.b12*b.b31 + a.b23*b.s   + a.b31*b.b12,
        a.s*b.b31 + a.b12*b.b23 - a.b23*b.b12 + a.b31*b.s,
    };
}

[[nodiscard]] constexpr Rotor reverse(Rotor R) noexcept {
    return Rotor{R.s, -R.b12, -R.b23, -R.b31};
}

}  // namespace aleph::math
