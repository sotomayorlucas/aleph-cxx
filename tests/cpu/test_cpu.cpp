#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "doctest.h"
#include <cstdint>
#include <sched.h>
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

TEST_CASE("CycleCounter measures nonzero, plausible core cycles") {
    // Hybrid-CPU (155H) caveat: a PERF_COUNT_HW_CPU_CYCLES event reads 0 while
    // the thread runs on an E-core. Pin to a P-core (cpu 2) so the measurement
    // is valid and this test is deterministic regardless of scheduler placement.
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(2, &cpus);
    REQUIRE(sched_setaffinity(0, sizeof(cpus), &cpus) == 0);
    CycleCounter ctr;
    constexpr std::uint64_t N = 1'000'000;
    std::uint64_t acc = 0;
    ctr.start();
    for (std::uint64_t i = 0; i < N; ++i) {
        acc += i;
        asm volatile("" : "+r"(acc));  // prevent the loop being optimized away
    }
    const std::uint64_t cyc = ctr.stop();
    CHECK(cyc > 0);
    CHECK(cyc < 100ULL * N);   // sane upper bound: < 100 cyc per trivial add
}
