module;
#include <cstdint>
#include <x86intrin.h>

export module aleph.cpu:cycles;

export namespace aleph::cpu {

// Read time-stamp counter. NOT serializing — out-of-order CPUs may
// reorder reads around it. Use rdtscp() when you need a fence.
[[gnu::always_inline]] inline std::uint64_t rdtsc() noexcept {
    return __rdtsc();
}

// Serializing variant: prevents earlier instructions from being moved
// after the read. Use to time short kernels.
[[gnu::always_inline]] inline std::uint64_t rdtscp() noexcept {
    unsigned aux;
    return __rdtscp(&aux);
}

[[gnu::always_inline]] inline void compiler_barrier() noexcept {
    asm volatile("" ::: "memory");
}

}  // namespace aleph::cpu
