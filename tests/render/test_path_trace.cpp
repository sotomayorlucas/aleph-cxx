#include "doctest.h"
import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::rt;
using aleph::math::Vec3;

TEST_CASE("path_trace: 16x16 simple scene → film has non-zero pixels") {
    aleph::scene::Scene s;
    const auto m_ground = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    aleph::scene::scene_add_sphere(s, Vec3{0, -1000, 0}, 1000.0f, m_ground);
    aleph::scene::scene_add_sphere(s, Vec3{0, 1, 0}, 1.0f, m_ground);
    alignas(16) static unsigned char scratch[16 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);

    alignas(64) static unsigned char film_buf[16 * 1024];
    aleph::alloc::Arena film_arena{film_buf, sizeof(film_buf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, 16, 16);

    const aleph::render::common::Camera cam =
        aleph::render::common::make_camera(Vec3{13, 2, 3}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                                            20.0f, 16, 16, 0.0f, 10.0f);
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};

    aleph::threads::Pool pool(2);
    path_trace(s, cam, sky, film, pool, RenderOpts{4, 5, 42, 8});
    int non_black = 0;
    for (int i = 0; i < film.width * film.height; ++i) {
        if (aleph::math::length_sq(film.pixels[i]) > 0.0f) ++non_black;
    }
    CHECK(non_black > 0);
}
