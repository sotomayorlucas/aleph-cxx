module;
#include <concepts>
#include <type_traits>

export module aleph.math:concepts;

import :types;

export namespace aleph::math {

// True iff T is a vector type with N components of the same scalar.
template<typename T, int N>
concept Vector = requires {
    typename T::scalar_type;
    requires std::is_trivially_copyable_v<T>;
    requires sizeof(T) >= sizeof(typename T::scalar_type) * N;
};

// True iff T behaves as a rotation: compose, identity, inverse exist.
template<typename T>
concept Rotation = requires(T a, T b) {
    { a * b } -> std::same_as<T>;
    { T::identity() } -> std::same_as<T>;
};

// Grade<k> marks elements of a specific G-algebra grade.
template<typename T, int K>
concept Grade = requires {
    requires T::grade == K;
};

}  // namespace aleph::math
