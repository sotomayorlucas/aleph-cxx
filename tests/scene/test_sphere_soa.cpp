#include "doctest.h"
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("SphereSoA: append + index lookup") {
    SphereSoA s;
    const auto i0 = sphere_append(s, Vec3{1, 2, 3}, 0.5f,
                                   MaterialHandle{MaterialKind::Lambertian, 0});
    const auto i1 = sphere_append(s, Vec3{4, 5, 6}, 1.5f,
                                   MaterialHandle{MaterialKind::Metal,      0});
    CHECK(i0 == 0u);
    CHECK(i1 == 1u);
    CHECK(s.cx.size() == 2);
    CHECK(s.cx[0] == 1.0f); CHECK(s.cy[0] == 2.0f); CHECK(s.cz[0] == 3.0f);
    CHECK(s.r[0]  == 0.5f);
    CHECK(s.cx[1] == 4.0f); CHECK(s.cz[1] == 6.0f); CHECK(s.r[1] == 1.5f);
    CHECK(s.mat[0].kind == MaterialKind::Lambertian);
    CHECK(s.mat[1].kind == MaterialKind::Metal);
}

TEST_CASE("SphereSoA: bbox cached per sphere") {
    SphereSoA s;
    sphere_append(s, Vec3{0, 0, 0}, 1.0f, MaterialHandle{MaterialKind::Lambertian, 0});
    REQUIRE(s.bbox.size() == 1);
    CHECK(s.bbox[0].min == Vec3{-1, -1, -1});
    CHECK(s.bbox[0].max == Vec3{ 1,  1,  1});
}
