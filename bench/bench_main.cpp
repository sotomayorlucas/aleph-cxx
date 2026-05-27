#include "bench_harness.hpp"
#include <cstdio>
#include <cstdint>

import aleph.math;
import aleph.alloc;
import aleph.threads;
import aleph.scene;
import aleph.render.rt;
import aleph.render.common;

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

    // hit_sphere — target <= 12 cycles
    {
        aleph::scene::SphereSoA s;
        aleph::scene::sphere_append(s, aleph::math::Vec3{0, 0, 0}, 1.0f,
            aleph::scene::MaterialHandle{aleph::scene::MaterialKind::Lambertian, 0});
        aleph::math::Ray r{aleph::math::Vec3{0, 0, -5}, aleph::math::Vec3{0, 0, 1}};
        aleph_bench::bench("hit_sphere (single)", [&](std::uint64_t iters) {
            std::uint64_t sink = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                auto h = aleph::scene::bench_hit_sphere(s, 0, r, 0.001f, 1e9f);
                if (h) sink ^= static_cast<std::uint64_t>(h->t * 1000.0f);
            }
            return sink;
        });
    }

    // hit_quad — target <= 25 cycles
    {
        aleph::scene::QuadSoA q;
        aleph::scene::quad_append(q, aleph::math::Vec3{-1, 0, 0},
            aleph::math::Vec3{2, 0, 0}, aleph::math::Vec3{0, 0, 2},
            aleph::scene::MaterialHandle{aleph::scene::MaterialKind::Lambertian, 0});
        aleph::math::Ray r{aleph::math::Vec3{0, 5, 1}, aleph::math::Vec3{0, -1, 0}};
        aleph_bench::bench("hit_quad (single)", [&](std::uint64_t iters) {
            std::uint64_t sink = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                auto h = aleph::scene::bench_hit_quad(q, 0, r, 0.001f, 1e9f);
                if (h) sink ^= static_cast<std::uint64_t>(h->t * 1000.0f);
            }
            return sink;
        });
    }

    // BVH traversal (100 items, miss) — target <= 200 cycles
    {
        aleph::scene::Scene s;
        const auto m = aleph::scene::scene_add_lambertian(s, aleph::math::Vec3{0.5f, 0.5f, 0.5f});
        aleph::render::common::Pcg32 rng(1, 1);
        for (int i = 0; i < 100; ++i) {
            aleph::scene::scene_add_sphere(s,
                aleph::math::Vec3{
                    rng.float01() * 10.0f - 5.0f,
                    rng.float01() * 10.0f - 5.0f,
                    rng.float01() * 10.0f - 5.0f},
                0.05f, m);
        }
        alignas(16) static unsigned char scratch[65536];
        aleph::alloc::Arena arena{scratch, sizeof(scratch)};
        aleph::scene::scene_build_bvh(s, arena);
        aleph::math::Ray r_miss{aleph::math::Vec3{100, 100, 100},
                                  aleph::math::Vec3{1, 0, 0}};
        aleph_bench::bench("BVH traversal (100 items, miss)", [&](std::uint64_t iters) {
            std::uint64_t sink = 0;
            for (std::uint64_t i = 0; i < iters; ++i) {
                auto h = aleph::scene::hit(s, r_miss, 0.001f, 1e9f);
                sink ^= h.has_value();
            }
            return sink;
        });
    }

    return 0;
}
