#include "doctest.h"
#include <cmath>
import aleph.scene;
import aleph.math;

using namespace aleph::scene;
using aleph::math::Vec3;

TEST_CASE("QuadSoA: append computes normal + D + w + bbox") {
    QuadSoA q;
    const auto i = quad_append(q, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1},
                                MaterialHandle{MaterialKind::Emissive, 0});
    CHECK(i == 0u);
    REQUIRE(q.Qx.size() == 1);
    CHECK(q.Qx[0] == 0.0f);  CHECK(q.Qy[0] == 0.0f);  CHECK(q.Qz[0] == 0.0f);
    CHECK(q.ux[0] == 1.0f);  CHECK(q.uy[0] == 0.0f);  CHECK(q.uz[0] == 0.0f);
    CHECK(q.vx[0] == 0.0f);  CHECK(q.vy[0] == 0.0f);  CHECK(q.vz[0] == 1.0f);
    // u × v = (1,0,0) × (0,0,1) = (0, -1, 0)
    CHECK(q.nx[0] == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(q.ny[0] == doctest::Approx(-1.0f).epsilon(1e-6));
    CHECK(q.nz[0] == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(q.D[0] == doctest::Approx(0.0f));
}

TEST_CASE("TriSoA: append stores three verts") {
    TriSoA t;
    const auto i = tri_append(t, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0},
                               MaterialHandle{MaterialKind::Lambertian, 0});
    CHECK(i == 0u);
    CHECK(t.v0x[0] == 0.0f); CHECK(t.v1x[0] == 1.0f); CHECK(t.v2y[0] == 1.0f);
    CHECK(t.mat[0].kind == MaterialKind::Lambertian);
}
