#include "bench_harness.hpp"
#include <cstdio>
#include <cstdint>

import aleph.math;
import aleph.alloc;
import aleph.threads;

using aleph::math::Vec3;
using aleph::math::Vec4;
using aleph::math::Mat4;
using aleph::math::Rotor;
using aleph::math::from_axis_angle;

int main() {
    aleph::cpu::assert_isa_compatible();
    std::printf("aleph-cxx foundation benchmarks (x86-64-v3, AVX2 + FMA)\n");
    std::printf("------------------------------------------------------\n");

    // Rotor compose — target <= 6 cycles
    {
        Rotor a = from_axis_angle({1, 0, 0}, 0.3f);
        const Rotor b = from_axis_angle({0, 1, 0}, 0.2f);
        aleph_bench::bench("Rotor compose", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) a = a * b;
            return a;
        });
    }

    // Vec3 dot — target <= 3 cycles
    {
        Vec3 a{1, 2, 3};
        const Vec3 b{4, 5, 6};
        aleph_bench::bench("Vec3 dot", [&](std::uint64_t iters) {
            float s = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                s += dot(a, b);
                a.x = s * 1e-6f;
            }
            return s;
        });
    }

    // Vec3 add — target <= 3 cycles
    {
        Vec3 a{0, 0, 0};
        const Vec3 b{1, 1, 1};
        aleph_bench::bench("Vec3 add", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) a = a + b;
            return a;
        });
    }

    // Mat4 * Vec4 — target <= 8 cycles
    {
        const Mat4 M = Mat4::perspective(1.0f, 16.0f/9.0f, 0.1f, 100.0f);
        Vec4 v{1, 2, 3, 1};
        aleph_bench::bench("Mat4 * Vec4", [&](std::uint64_t iters) {
            for (std::uint64_t i = 0; i < iters; ++i) v = M * v;
            return v;
        });
    }

    // Arena allocate(64) — target <= 3 cycles
    {
        alignas(64) static unsigned char buf[1 << 20];
        aleph::alloc::Arena arena{buf, sizeof(buf)};
        aleph_bench::bench("Arena allocate(64)", [&](std::uint64_t iters) {
            std::uintptr_t sink = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                if (arena.bytes_in_use() + 64 > arena.capacity()) arena.reset();
                sink ^= reinterpret_cast<std::uintptr_t>(arena.allocate(64, 16));
            }
            return sink;
        });
    }

    // MpmcRing<u64,1024> uncontended push+pop — target ~10 ns (~40 cyc @ 4 GHz)
    {
        aleph::threads::MpmcRing<std::uint64_t, 1024> q;
        aleph_bench::bench("MpmcRing<u64,1024> push+pop", [&](std::uint64_t iters) {
            std::uint64_t out = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                (void)q.try_push(i);
                (void)q.try_pop(out);
            }
            return out;
        });
    }

    return 0;
}
