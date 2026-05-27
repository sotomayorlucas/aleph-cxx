#include "doctest.h"
import aleph.editor;
import aleph.render.sw;
import aleph.math;

using namespace aleph::editor;

TEST_CASE("pick_face: ray hits a known face → returns its index") {
    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, aleph::math::Vec3{0, 0, 0}, 2.0f,
                                  aleph::render::sw::tex_floor);
    const int i = pick_face(sr, 400, 300,
                              aleph::math::Vec3{0, 5, 0},
                              aleph::math::Vec3{0, 0, 0},
                              aleph::math::Vec3{0, 0, -1},
                              aleph::math::deg_to_rad(60.0f), 4.0f/3.0f, 800, 600);
    CHECK(i == 0);
}
