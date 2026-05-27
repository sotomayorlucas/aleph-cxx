#include "bench_harness.hpp"
#include <cstdio>

int main() {
    aleph::cpu::assert_isa_compatible();
    std::printf("aleph-cxx foundation benchmarks (x86-64-v3, AVX2 + FMA)\n");
    std::printf("------------------------------------------------------\n");
    // Benchmark cases added in Task 31.
    return 0;
}
