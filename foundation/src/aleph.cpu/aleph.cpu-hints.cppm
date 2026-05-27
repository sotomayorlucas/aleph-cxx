module;
#include <xmmintrin.h>

export module aleph.cpu:hints;

export namespace aleph::cpu {

// Software prefetch into L1. Use 1 cache line ahead in tight inner loops.
template<typename T>
[[gnu::always_inline]] inline void prefetch(const T* p) noexcept {
    __builtin_prefetch(p, /*rw=*/0, /*locality=*/3);
}

template<typename T>
[[gnu::always_inline]] inline void prefetch_write(T* p) noexcept {
    __builtin_prefetch(p, /*rw=*/1, /*locality=*/3);
}

}  // namespace aleph::cpu

// Branch hints. Macros only because __builtin_expect needs to wrap a
// conditional expression directly.
#define ALEPH_LIKELY(x)   __builtin_expect(!!(x), 1)
#define ALEPH_UNLIKELY(x) __builtin_expect(!!(x), 0)
