module;
#include <cmath>
#include <algorithm>

export module aleph.editor:orbit;

import aleph.math;
import aleph.window;

export namespace aleph::editor {

struct OrbitCam {
    aleph::math::Vec3 target{0, 0, 0};
    aleph::math::f32  yaw{0.0f};
    aleph::math::f32  pitch{0.0f};
    aleph::math::f32  radius{5.0f};
};

inline aleph::math::Vec3 orbit_eye(const OrbitCam& c) noexcept {
    return aleph::math::Vec3{
        c.target.x + c.radius * std::sin(c.yaw) * std::cos(c.pitch),
        c.target.y + c.radius * std::sin(c.pitch),
        c.target.z + c.radius * std::cos(c.yaw) * std::cos(c.pitch),
    };
}

inline bool orbit_handle(OrbitCam& c, const aleph::window::Event& e,
                          bool left_down, bool right_down) noexcept {
    using aleph::math::f32;
    constexpr f32 ORBIT_SPEED = 0.008f;
    constexpr f32 PAN_SPEED   = 0.012f;
    constexpr f32 ZOOM_FACTOR = 1.12f;
    switch (e.kind) {
        case aleph::window::Event::Kind::MouseMove: {
            if (left_down) {
                c.yaw   -= static_cast<f32>(e.dx) * ORBIT_SPEED;
                c.pitch -= static_cast<f32>(e.dy) * ORBIT_SPEED;
                c.pitch = std::clamp(c.pitch, -1.5f, 1.5f);
                return true;
            } else if (right_down) {
                const aleph::math::Vec3 fwd{
                    std::sin(c.yaw) * std::cos(c.pitch),
                    std::sin(c.pitch),
                    std::cos(c.yaw) * std::cos(c.pitch),
                };
                const aleph::math::Vec3 right = aleph::math::normalize(
                    aleph::math::cross(fwd, aleph::math::Vec3{0, 1, 0}));
                const aleph::math::Vec3 up = aleph::math::normalize(
                    aleph::math::cross(right, fwd));
                const f32 scale = c.radius * PAN_SPEED;
                c.target = c.target + right * (-static_cast<f32>(e.dx) * scale);
                c.target = c.target + up    * ( static_cast<f32>(e.dy) * scale);
                return true;
            }
            return false;
        }
        case aleph::window::Event::Kind::MouseWheel: {
            if (e.wheel > 0)      c.radius /= ZOOM_FACTOR;
            else if (e.wheel < 0) c.radius *= ZOOM_FACTOR;
            c.radius = std::clamp(c.radius, 0.5f, 40.0f);
            return true;
        }
        default: return false;
    }
}

}  // namespace aleph::editor
