#include "doctest.h"
import aleph.editor;
import aleph.window;
import aleph.math;

using namespace aleph::editor;

TEST_CASE("orbit_eye: target=origin, radius=5, yaw=0, pitch=0 → eye at (0,0,5)") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 5.0f};
    aleph::math::Vec3 e = orbit_eye(c);
    CHECK(e.z == doctest::Approx(5.0f));
    CHECK(e.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(e.y == doctest::Approx(0.0f).epsilon(1e-5f));
}

TEST_CASE("orbit_handle: MouseMove with no buttons → no change") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.3f, 0.25f, 8.0f};
    aleph::window::Event e{};
    e.kind = aleph::window::Event::Kind::MouseMove;
    e.dx = 10; e.dy = 5;
    const auto before = c;
    orbit_handle(c, e, false, false);
    CHECK(c.yaw == before.yaw);
    CHECK(c.pitch == before.pitch);
}

TEST_CASE("orbit_handle: MouseWheel zooms radius") {
    OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 8.0f};
    aleph::window::Event e{};
    e.kind = aleph::window::Event::Kind::MouseWheel;
    e.wheel = 1;
    orbit_handle(c, e, false, false);
    CHECK(c.radius < 8.0f);
}
