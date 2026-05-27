#include "doctest.h"
import aleph.render.common;
import aleph.math;

using namespace aleph::render::common;
using aleph::math::Vec3;

TEST_CASE("make_camera: pinhole when defocus_angle <= 0") {
    Camera c = make_camera(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                            60.0f, 100, 100, 0.0f, 1.0f);
    CHECK_FALSE(c.has_defocus);
    CHECK(c.center == Vec3{0, 0, 5});
}

TEST_CASE("camera_get_ray: pixel (50,50) on a 100x100 image maps near forward") {
    Camera c = make_camera(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0},
                            60.0f, 100, 100, 0.0f, 1.0f);
    Pcg32 rng(42, 54);
    aleph::math::Ray r = camera_get_ray(c, 50, 50, rng);
    CHECK(r.origin == Vec3{0, 0, 5});
    CHECK(r.dir.z < 0.0f);
}

TEST_CASE("sky_sample: gradient interpolates between low and high") {
    Sky s{ Vec3{0, 0, 0}, Vec3{1, 1, 1} };
    Vec3 c = sky_sample(s, Vec3{0, 1, 0});
    CHECK(c == Vec3{1, 1, 1});
    c = sky_sample(s, Vec3{0, -1, 0});
    CHECK(c == Vec3{0, 0, 0});
}
