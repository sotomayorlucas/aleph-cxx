#include "doctest.h"
#include <vector>
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::sw;

TEST_CASE("rasterize: SceneRT with one floor → film has non-magenta pixels") {
    SceneRT sr;
    add_floor(sr, aleph::math::Vec3{0, 0, 0}, 4.0f, tex_floor);
    sr.lightmap_pool.resize(32 * 32, 0xFFFFFFFFu);
    sr.lightmaps[0].texels = sr.lightmap_pool.data();
    sr.lightmaps[0].w = 32; sr.lightmaps[0].h = 32;
    sr.lightmaps[0].u_min = -2.0f; sr.lightmaps[0].u_max = 2.0f;
    sr.lightmaps[0].v_min = -2.0f; sr.lightmaps[0].v_max = 2.0f;

    alignas(64) static unsigned char fbuf[32 * 32 * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(arena, 32, 32);
    for (int i = 0; i < 32 * 32; ++i) film.pixels[i] = aleph::math::Vec3{1, 0, 1};
    std::vector<aleph::math::f32> depth(32 * 32, 1.0f);

    const aleph::math::Mat4 view = aleph::math::Mat4::look_at(
        aleph::math::Vec3{0, 2, 5}, aleph::math::Vec3{0, 0, 0}, aleph::math::Vec3{0, 1, 0});
    const aleph::math::Mat4 proj = aleph::math::Mat4::perspective(
        aleph::math::deg_to_rad(60.0f), 1.0f, 0.05f, 100.0f);
    const aleph::math::Mat4 mvp = proj * view;

    aleph::threads::Pool pool(2);
    rasterize(sr, mvp, film, depth, pool);

    int touched = 0;
    for (int i = 0; i < 32 * 32; ++i)
        if (!(film.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    CHECK(touched > 0);
}
