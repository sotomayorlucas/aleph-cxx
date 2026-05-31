#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bench_harness.hpp"
#include <cstdio>
#include <cstdint>
#include <sched.h>

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
    // Pin to a P-core (cpu 2): on the hybrid Core Ultra 7 155H a hardware-cycles
    // perf event reads 0 on E-cores, so a consistent P-core is required for
    // valid, stable measurements (run-baselines.sh also pins — belt and braces).
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(2, &cpus);
    sched_setaffinity(0, sizeof(cpus), &cpus);

    // NOTE: the throughput benches below run `iters / 8` outer iterations x 8
    // lanes = `iters` ops, so the harness's `cycles / iters` is true per-op.
    // This assumes `iters` is a multiple of 8 — the default (100000) is.

    aleph::cpu::assert_isa_compatible();
    std::printf("aleph-cxx foundation benchmarks (x86-64-v3, AVX2 + FMA)\n");
    std::printf("------------------------------------------------------\n");

    // Rotor compose — throughput, 8 mutually-independent unit-rotor product
    // chains (independent across lanes). Unit*unit stays unit-norm, so values
    // never degenerate. Lanes are re-seeded from a0[] each sample so every
    // sample measures identical work (matches the other benches' fresh-state).
    {
        const Rotor b = from_axis_angle({0, 1, 0}, 0.2f);
        Rotor a0[8];
        for (int k = 0; k < 8; ++k)
            a0[k] = from_axis_angle({1, 0, 0}, 0.3f + 0.01f * static_cast<float>(k));
        aleph_bench::bench("Rotor compose", [&](std::uint64_t iters) {
            Rotor a[8];
            for (int k = 0; k < 8; ++k) a[k] = a0[k];
            for (std::uint64_t i = 0; i < iters / 8; ++i)  // 8 lanes x iters/8 = iters ops
                for (int k = 0; k < 8; ++k) a[k] = a[k] * b;
            float acc = 0.0f;
            for (int k = 0; k < 8; ++k) acc += a[k].s;  // escape all lanes
            return acc;
        });
    }

    // Vec3 dot — throughput, 8 mutually-independent feedback chains (each lane a
    // short feedback chain; lanes independent of each other for ILP).
    // a[k].x = s[k]*1e-6 keeps each dot non-hoistable; s[k] stays small.
    {
        Vec3 a[8];
        Vec3 b[8];
        for (int k = 0; k < 8; ++k) {
            a[k] = Vec3{1 + 0.1f * static_cast<float>(k), 2, 3};
            b[k] = Vec3{4, 5, 6};
        }
        aleph_bench::bench("Vec3 dot", [&](std::uint64_t iters) {
            float s[8]{};
            for (std::uint64_t i = 0; i < iters / 8; ++i)  // 8 lanes x iters/8 = iters ops
                for (int k = 0; k < 8; ++k) {
                    s[k] += dot(a[k], b[k]);
                    a[k].x = s[k] * 1e-6f;
                }
            float acc = 0.0f;
            for (int k = 0; k < 8; ++k) acc += s[k];
            return acc;
        });
    }

    // Vec3 add — throughput, 8 independent accumulators (each += b each iter,
    // so never hoisted; magnitude ~iters, bounded).
    {
        const Vec3 b{1, 1, 1};
        aleph_bench::bench("Vec3 add", [&](std::uint64_t iters) {
            Vec3 acc[8]{};
            for (std::uint64_t i = 0; i < iters / 8; ++i)  // 8 lanes x iters/8 = iters ops
                for (int k = 0; k < 8; ++k) acc[k] = acc[k] + b;
            Vec3 r = acc[0];
            for (int k = 1; k < 8; ++k) r = r + acc[k];
            return r;
        });
    }

    // Mat4 * Vec4 — throughput, 8 independent matvecs accumulated per lane.
    // v[k].x varies with i (so M*v[k] is not hoisted) but stays in [1, ~1.1];
    // acc[k] grows to ~iters, bounded, no inf/denormal.
    {
        const Mat4 M = Mat4::perspective(1.0f, 16.0f/9.0f, 0.1f, 100.0f);
        Vec4 v[8];
        for (int k = 0; k < 8; ++k) v[k] = Vec4{1.0f + 0.1f * static_cast<float>(k), 2, 3, 1};
        aleph_bench::bench("Mat4 * Vec4", [&](std::uint64_t iters) {
            Vec4 acc[8]{};
            for (std::uint64_t i = 0; i < iters / 8; ++i)  // 8 lanes x iters/8 = iters ops
                for (int k = 0; k < 8; ++k) {
                    v[k].x = 1.0f + 1e-6f * static_cast<float>(i + static_cast<std::uint64_t>(k));
                    acc[k] = acc[k] + M * v[k];
                }
            Vec4 r = acc[0];
            for (int k = 1; k < 8; ++k) r = r + acc[k];
            return r;
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
