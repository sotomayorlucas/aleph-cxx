#include "doctest.h"
import aleph.cpu;

using namespace aleph::cpu;

TEST_CASE("cpu constants reflect build flags") {
    static_assert(has_avx2);
    static_assert(has_fma);
    static_assert(cache_line == 64);
    CHECK(has_avx2);
    CHECK(has_fma);
}

TEST_CASE("assert_isa_compatible does not abort on AVX2 host") {
    assert_isa_compatible();   // would abort if AVX2 missing
    CHECK(true);
}

TEST_CASE("rdtsc / rdtscp are monotonic across consecutive calls") {
    const auto a = rdtsc();
    for (int i = 0; i < 1000; ++i) {
        asm volatile("" ::: "memory");  // discourage hoisting
    }
    const auto b = rdtsc();
    CHECK(b > a);

    const auto c = rdtscp();
    for (int i = 0; i < 1000; ++i) asm volatile("" ::: "memory");
    const auto d = rdtscp();
    CHECK(d > c);
}

TEST_CASE("prefetch is a no-op effect-wise (compile + run smoke)") {
    int data[64]{};
    prefetch(data);            // shouldn't crash
    prefetch(data + 32);
    CHECK(true);
}
