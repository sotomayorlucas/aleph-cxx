#include "doctest.h"
import aleph.render.sw;
import aleph.math;

using namespace aleph::render::sw;

TEST_CASE("lightmap_sample_bilinear: solid white lightmap returns white") {
    aleph::math::u32 texels[16];
    for (auto& t : texels) t = 0xFFFFFFFFu;
    Lightmap lm{texels, 4, 4, 0.0f, 1.0f, 0.0f, 1.0f};
    const aleph::math::u32 s = lightmap_sample_bilinear(lm, 0.5f, 0.5f);
    CHECK(((s >> 16) & 0xFFu) == 0xFFu);
    CHECK(((s >>  8) & 0xFFu) == 0xFFu);
    CHECK((s        & 0xFFu) == 0xFFu);
}

TEST_CASE("lightmap_bake: face + one light → texels in expected range") {
    SceneRT sr;
    Face floor{};
    floor.verts = {aleph::math::Vec3{-1, 0, -1}, aleph::math::Vec3{1, 0, -1},
                   aleph::math::Vec3{1, 0, 1},  aleph::math::Vec3{-1, 0, 1}};
    floor.uvs   = {aleph::math::Vec2{0,0}, aleph::math::Vec2{1,0},
                   aleph::math::Vec2{1,1}, aleph::math::Vec2{0,1}};
    floor.tex   = tex_floor;
    floor.lightmap_id = 0;
    sr.faces.push_back(floor);
    sr.lightmaps.push_back(Lightmap{});

    const int LM = 8;
    sr.lightmap_pool.resize(LM * LM, 0u);
    sr.lightmaps[0].texels = sr.lightmap_pool.data();
    sr.lightmaps[0].w = LM;
    sr.lightmaps[0].h = LM;

    bake_lightmaps(sr, aleph::math::Vec3{0, 5, 0}, 10.0f, 0.05f);
    bool any_lit = false;
    for (auto t : sr.lightmap_pool)
        if ((t & 0xFFu) > 0u) { any_lit = true; break; }
    CHECK(any_lit);
}
