#include "doctest.h"
#include <limits>
import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::math::Ray;

TEST_CASE("ray_color: miss returns sky") {
    aleph::scene::Scene s;
    alignas(16) static unsigned char scratch[4096];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{0.5f, 0.7f, 1.0f}};
    aleph::render::common::Pcg32 rng(42, 0);
    Vec3 c = ray_color(s, Ray{Vec3{0, 10, 0}, Vec3{0, 1, 0}}, 5, sky, true, rng);
    CHECK(c == sky.high);   // pure +y direction → sky.high
}

TEST_CASE("ray_color depth=0 returns black") {
    aleph::scene::Scene s;
    aleph::render::common::Sky sky{Vec3{1, 1, 1}, Vec3{1, 1, 1}};
    aleph::render::common::Pcg32 rng(0, 0);
    Vec3 c = ray_color(s, Ray{Vec3{0, 0, 0}, Vec3{0, 1, 0}}, 0, sky, true, rng);
    CHECK(c == Vec3{0, 0, 0});
}
