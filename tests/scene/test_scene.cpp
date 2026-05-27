#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("Scene starts empty") {
    Scene s;
    CHECK(s.spheres.cx.empty());
    CHECK(s.lights.empty());
    CHECK(s.textures.empty());
}

TEST_CASE("scene_add_sphere returns Handle32 with correct kind+idx") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    const auto h = scene_add_sphere(s, Vec3{0, 0, 0}, 1.0f, m);
    CHECK(h.hittable_kind() == HittableKind::Sphere);
    CHECK(h.index() == 0u);
    CHECK(s.spheres.cx.size() == 1);
}

TEST_CASE("scene_add_quad with emissive material auto-registers in lights list") {
    Scene s;
    const auto m = scene_add_emissive(s, Vec3{15, 15, 15});
    const auto h = scene_add_quad(s, Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}, m);
    CHECK(h.hittable_kind() == HittableKind::Quad);
    REQUIRE(s.lights.size() == 1);
    CHECK(s.lights[0].packed == h.packed);
}

TEST_CASE("Non-emissive quad does NOT add to lights") {
    Scene s;
    const auto m = scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    scene_add_quad(s, Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}, m);
    CHECK(s.lights.empty());
}

TEST_CASE("scene_add_metal returns material handle with correct kind") {
    Scene s;
    const auto m = scene_add_metal(s, Vec3{0.7f, 0.6f, 0.5f}, 0.1f);
    CHECK(m.kind == MaterialKind::Metal);
    CHECK(m.idx == 0u);
}
