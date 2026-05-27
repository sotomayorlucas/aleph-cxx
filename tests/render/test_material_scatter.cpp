#include "doctest.h"
#include <optional>
import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::math::Ray;
using aleph::render::common::Pcg32;
using aleph::scene::Scene;
using aleph::scene::MaterialHandle;
using aleph::scene::MaterialKind;
using aleph::scene::HitRecord;

TEST_CASE("scatter Lambertian: produces scattered ray + albedo attenuation") {
    Scene s;
    const auto m = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.3f, 0.7f});
    HitRecord rec{};
    rec.p = Vec3{0, 0, 0};
    rec.normal = Vec3{0, 1, 0};
    rec.front_face = true;
    rec.mat = m;
    Pcg32 rng(42, 0);
    auto r = scatter(s, Ray{Vec3{0, 1, 0}, Vec3{0, -1, 0}}, rec, rng);
    REQUIRE(r.has_value());
    CHECK(r->attenuation == Vec3{0.5f, 0.3f, 0.7f});
    CHECK(r->scattered.origin == Vec3{0, 0, 0});
}

TEST_CASE("scatter Emissive: returns nullopt (no scatter)") {
    Scene s;
    const auto m = aleph::scene::scene_add_emissive(s, Vec3{15, 15, 15});
    HitRecord rec{};
    rec.mat = m;
    Pcg32 rng(0, 0);
    auto r = scatter(s, Ray{}, rec, rng);
    CHECK_FALSE(r.has_value());
}

TEST_CASE("emitted: Emissive returns emit vec, others return zero") {
    Scene s;
    const auto e   = aleph::scene::scene_add_emissive(s, Vec3{15, 15, 15});
    const auto lam = aleph::scene::scene_add_lambertian(s, Vec3{0.5f, 0.5f, 0.5f});
    CHECK(emitted(s, e)   == Vec3{15, 15, 15});
    CHECK(emitted(s, lam) == Vec3{0, 0, 0});
}
