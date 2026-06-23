#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import aleph.math;
import aleph.scene;
import aleph.render.rt;
import aleph.render.common;
import aleph.alloc;
import aleph.threads;
import aleph.io;

namespace {

using aleph::math::Vec3;
using aleph::math::f32;
using aleph::math::u32;
using aleph::math::u64;

void build_cover(aleph::scene::Scene& s, aleph::render::common::Pcg32& rng) {
    const auto m_ground = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    aleph::scene::scene_add_sphere(s, Vec3{0, -1000, 0}, 1000.0f, m_ground);
    for (int a = -11; a < 11; ++a) for (int b = -11; b < 11; ++b) {
        const f32 choose = rng.float01();
        const Vec3 c{static_cast<f32>(a) + 0.9f * rng.float01(),
                     0.2f,
                     static_cast<f32>(b) + 0.9f * rng.float01()};
        if (aleph::math::length(c - Vec3{4, 0.2f, 0}) <= 0.9f) continue;
        aleph::scene::MaterialHandle m;
        if (choose < 0.8f) {
            m = aleph::scene::scene_add_lambertian(s,
                Vec3{rng.float01() * rng.float01(),
                     rng.float01() * rng.float01(),
                     rng.float01() * rng.float01()});
        } else if (choose < 0.95f) {
            m = aleph::scene::scene_add_metal(s,
                Vec3{0.5f + 0.5f * rng.float01(),
                     0.5f + 0.5f * rng.float01(),
                     0.5f + 0.5f * rng.float01()},
                0.5f * rng.float01());
        } else {
            m = aleph::scene::scene_add_dielectric(s, 1.5f);
        }
        aleph::scene::scene_add_sphere(s, c, 0.2f, m);
    }
    aleph::scene::scene_add_sphere(s, Vec3{0, 1, 0}, 1.0f,
        aleph::scene::scene_add_dielectric(s, 1.5f));
    aleph::scene::scene_add_sphere(s, Vec3{-4, 1, 0}, 1.0f,
        aleph::scene::scene_add_lambertian(s, Vec3{0.4f, 0.2f, 0.1f}));
    aleph::scene::scene_add_sphere(s, Vec3{4, 1, 0}, 1.0f,
        aleph::scene::scene_add_metal(s, Vec3{0.7f, 0.6f, 0.5f}, 0.0f));
}

void build_cornell(aleph::scene::Scene& s) {
    const auto RED   = aleph::scene::scene_add_lambertian(s, Vec3{0.65f, 0.05f, 0.05f});
    const auto GREEN = aleph::scene::scene_add_lambertian(s, Vec3{0.12f, 0.45f, 0.15f});
    const auto WHITE = aleph::scene::scene_add_lambertian(s, Vec3{0.73f, 0.73f, 0.73f});
    const auto LIGHT = aleph::scene::scene_add_emissive  (s, Vec3{15.0f, 15.0f, 15.0f});
    aleph::scene::scene_add_quad(s, Vec3{555, 0, 0},   Vec3{0, 555, 0}, Vec3{0, 0, 555}, GREEN);
    aleph::scene::scene_add_quad(s, Vec3{0,   0, 0},   Vec3{0, 555, 0}, Vec3{0, 0, 555}, RED);
    aleph::scene::scene_add_quad(s, Vec3{343, 554, 332}, Vec3{-130, 0, 0}, Vec3{0, 0, -105}, LIGHT);
    aleph::scene::scene_add_quad(s, Vec3{0,   0,   0}, Vec3{555, 0, 0}, Vec3{0, 0, 555}, WHITE);
    aleph::scene::scene_add_quad(s, Vec3{555, 555, 0}, Vec3{-555, 0, 0}, Vec3{0, 0, 555}, WHITE);
    aleph::scene::scene_add_quad(s, Vec3{0, 0, 555},   Vec3{555, 0, 0}, Vec3{0, 555, 0}, WHITE);
    auto add_box = [&](Vec3 mn, Vec3 mx, aleph::scene::MaterialHandle mat) {
        const f32 dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mx.z}, Vec3{ dx, 0, 0}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mx.x, mn.y, mn.z}, Vec3{-dx, 0, 0}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mx.x, mn.y, mx.z}, Vec3{0, 0, -dz}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mn.z}, Vec3{0, 0,  dz}, Vec3{0, dy, 0}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mx.y, mx.z}, Vec3{ dx, 0, 0}, Vec3{0, 0, -dz}, mat);
        aleph::scene::scene_add_quad(s, Vec3{mn.x, mn.y, mn.z}, Vec3{ dx, 0, 0}, Vec3{0, 0,  dz}, mat);
    };
    add_box(Vec3{265, 0,  295}, Vec3{430, 330, 460}, WHITE);
    add_box(Vec3{130, 0,   65}, Vec3{295, 165, 230}, WHITE);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: aleph_rt <out.ppm> <cover|cornell> [spp] [depth] [seed] [threads]\n");
        return 1;
    }
    const std::string_view out_path = argv[1];
    const std::string_view scene_s  = argv[2];
    const int spp     = argc > 3 ? std::atoi(argv[3]) : 16;
    const int depth   = argc > 4 ? std::atoi(argv[4]) : 8;
    const u64 seed    = argc > 5 ? static_cast<u64>(std::atoll(argv[5])) : 42ull;
    int       threads = argc > 6 ? std::atoi(argv[6]) : 0;
    if (threads <= 0) threads = static_cast<int>(std::thread::hardware_concurrency());
    if (threads <= 0) threads = 1;

    int W = 400, H = 225;
    Vec3 lookfrom{13, 2, 3}, lookat{0, 0, 0};
    f32 vfov = 20.0f, defocus = 0.6f, focus_dist = 10.0f;
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};

    aleph::scene::Scene scene;
    if (scene_s == "cover") {
        aleph::render::common::Pcg32 rng(1u, 1u);
        build_cover(scene, rng);
    } else if (scene_s == "cornell") {
        W = H = 400;
        lookfrom = Vec3{278, 278, -800};
        lookat   = Vec3{278, 278, 0};
        vfov = 40.0f; defocus = 0.0f; focus_dist = 800.0f;
        sky = aleph::render::common::Sky{Vec3{0, 0, 0}, Vec3{0, 0, 0}};
        build_cornell(scene);
    } else {
        std::fprintf(stderr, "aleph_rt: unknown scene '%.*s'\n",
                      static_cast<int>(scene_s.size()), scene_s.data());
        return 1;
    }

    static unsigned char bvh_scratch[1 << 20];
    aleph::alloc::Arena bvh_arena{bvh_scratch, sizeof(bvh_scratch)};
    aleph::scene::scene_build_bvh(scene, bvh_arena);

    alignas(64) static unsigned char film_scratch[400 * 400 * sizeof(Vec3)];
    aleph::alloc::Arena film_arena{film_scratch, sizeof(film_scratch)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);

    const aleph::render::common::Camera cam = aleph::render::common::make_camera(
        lookfrom, lookat, Vec3{0, 1, 0}, vfov, W, H, defocus, focus_dist);

    aleph::threads::Pool pool(threads);
    aleph::render::rt::path_trace(scene, cam, sky, film, pool,
        aleph::render::rt::RenderOpts{spp, depth, seed, 32});

    // Convert linear Film (Vec3) to PPM bytes (sRGB OETF).
    // We use aleph.io's write_ppm which expects a different format; write directly here.
    std::string p{out_path};
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "aleph_rt: cannot open %s for writing\n", p.c_str());
        return 1;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Vec3 lin = film.pixels[y * film.stride_pixels + x];
            const unsigned char rgb[3] = {
                aleph::render::common::byte_from_linear(lin.x),
                aleph::render::common::byte_from_linear(lin.y),
                aleph::render::common::byte_from_linear(lin.z),
            };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return 0;
}
