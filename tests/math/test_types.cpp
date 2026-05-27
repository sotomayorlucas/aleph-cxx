#include "doctest.h"
import aleph.math;

using namespace aleph::math;

TEST_CASE("scalar aliases have the expected widths") {
    static_assert(sizeof(u8)  == 1);
    static_assert(sizeof(u16) == 2);
    static_assert(sizeof(u32) == 4);
    static_assert(sizeof(u64) == 8);
    static_assert(sizeof(i32) == 4);
    static_assert(sizeof(f32) == 4);
    static_assert(sizeof(f64) == 8);
    CHECK(true);
}

TEST_CASE("approx_eq scalar") {
    CHECK(approx_eq(1.0f, 1.0f, 0.0f));
    CHECK(approx_eq(1.0f, 1.0f + 1e-9f, 1e-6f));
    CHECK_FALSE(approx_eq(1.0f, 1.1f, 1e-6f));
}
