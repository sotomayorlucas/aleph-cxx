#include "doctest.h"
#include <limits>
import aleph.scene;
import aleph.math;
import aleph.alloc;

using namespace aleph::scene;
using aleph::math::Vec3;
using aleph::math::Ray;

TEST_CASE("hit on empty scene returns nullopt") {
    Scene s;
    alignas(16) static unsigned char scratch[1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    auto r = hit(s, Ray{Vec3{0,0,-5}, Vec3{0,0,1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    CHECK_FALSE(r.has_value());
}

TEST_CASE("hit: single sphere — ray straight in hits it") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    alignas(16) static unsigned char scratch[2048];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    auto r = hit(s, Ray{Vec3{0, 0, -5}, Vec3{0, 0, 1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    REQUIRE(r.has_value());
    CHECK(r->t == doctest::Approx(4.0f));
    CHECK(r->front_face);
    CHECK(r->mat.kind == MaterialKind::Lambertian);
}

TEST_CASE("hit: 3 spheres in a row — closest one returned") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_sphere(s, Vec3{0, 0,  5}, 0.5f, m);
    scene_add_sphere(s, Vec3{0, 0,  0}, 0.5f, m);
    scene_add_sphere(s, Vec3{0, 0, -5}, 0.5f, m);
    alignas(16) static unsigned char scratch[4096];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    scene_build_bvh(s, arena);
    // Ray from -10 going +Z: should hit z=-5 sphere first (t≈4.5).
    auto r = hit(s, Ray{Vec3{0, 0, -10}, Vec3{0, 0, 1}}, 0.001f,
                  std::numeric_limits<aleph::math::f32>::infinity());
    REQUIRE(r.has_value());
    CHECK(r->t == doctest::Approx(4.5f).epsilon(1e-5f));
}
