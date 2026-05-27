module;
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cpuid.h>

export module aleph.cpu:isa;

export namespace aleph::cpu {

inline constexpr bool has_avx2     = true;   // required at build time
inline constexpr bool has_fma      = true;
inline constexpr bool has_bmi2     = true;
inline constexpr bool has_avx_vnni = false;  // optional fast path probe
inline constexpr int  cache_line   = 64;

// Runtime CPUID probe. Aborts with a clear message if any required ISA
// feature is missing on the host CPU. Should be called once from main().
[[gnu::cold]] inline void assert_isa_compatible() {
    unsigned a, b, c, d;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d) == 0) {
        std::fprintf(stderr, "aleph.cpu: CPUID leaf 7 unavailable — abort.\n");
        std::abort();
    }
    const bool runtime_avx2 = (b >> 5) & 1u;
    const bool runtime_bmi2 = (b >> 8) & 1u;
    unsigned a1 = 0, b1 = 0, c1 = 0, d1 = 0;
    __get_cpuid(1, &a1, &b1, &c1, &d1);
    const bool runtime_fma = (c1 >> 12) & 1u;
    if (!runtime_avx2 || !runtime_fma || !runtime_bmi2) {
        std::fprintf(stderr,
            "aleph.cpu: required ISA features missing on host "
            "(avx2=%d fma=%d bmi2=%d). Rebuild for an older baseline.\n",
            runtime_avx2, runtime_fma, runtime_bmi2);
        std::abort();
    }
}

}  // namespace aleph::cpu
