#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;
using aleph::math::Vec2;

TEST_CASE("LambertianSoA append + lookup") {
    LambertianSoA l;
    const auto i = lambertian_append(l, Vec3{0.5f, 0.3f, 0.1f});
    CHECK(i == 0u);
    CHECK(l.albedo[0] == Vec3{0.5f, 0.3f, 0.1f});
}

TEST_CASE("MetalSoA append: albedo + fuzz") {
    MetalSoA m;
    const auto i = metal_append(m, Vec3{0.7f, 0.7f, 0.7f}, 0.2f);
    CHECK(i == 0u);
    CHECK(m.albedo[0] == Vec3{0.7f, 0.7f, 0.7f});
    CHECK(m.fuzz[0] == 0.2f);
}

TEST_CASE("DielectricSoA: just ior") {
    DielectricSoA d;
    const auto i = dielectric_append(d, 1.5f);
    CHECK(i == 0u);
    CHECK(d.ior[0] == 1.5f);
}

TEST_CASE("EmissiveSoA: emit color") {
    EmissiveSoA e;
    const auto i = emissive_append(e, Vec3{15, 15, 15});
    CHECK(i == 0u);
    CHECK(e.emit[0] == Vec3{15, 15, 15});
}

TEST_CASE("TexturedLambertianSoA: albedo + tex_id + uv_scale") {
    TexturedLambertianSoA t;
    const auto i = textured_lambertian_append(t, Vec3{0.2f, 0.4f, 0.6f}, 7u, Vec2{2.0f, 1.0f});
    CHECK(i == 0u);
    CHECK(t.albedo[0] == Vec3{0.2f, 0.4f, 0.6f});
    CHECK(t.tex_id[0] == 7u);
    CHECK(t.uv_scale[0] == Vec2{2.0f, 1.0f});
}
