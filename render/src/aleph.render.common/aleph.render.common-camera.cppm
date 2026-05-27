module;
#include <cmath>
#include <cstdint>

export module aleph.render.common:camera;

import aleph.math;

export namespace aleph::render::common {

// Simple PCG32 RNG for camera sampling
struct Pcg32 {
    std::uint64_t state;
    std::uint64_t inc;

    explicit Pcg32(std::uint64_t initstate, std::uint64_t initseq = 1u) noexcept
        : state(initstate + initseq), inc(initseq << 1u | 1u) {
        float01();  // burn initial value
    }

    std::uint32_t next() noexcept {
        std::uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ull + inc;
        std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        std::uint32_t rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << (32u - rot));
    }

    float float01() noexcept {
        return (next() >> 8u) * (1.0f / 16777216.0f);
    }
};

struct Camera {
    aleph::math::Vec3 center{};
    aleph::math::Vec3 pixel00_loc{};
    aleph::math::Vec3 pixel_delta_u{};
    aleph::math::Vec3 pixel_delta_v{};
    aleph::math::Vec3 defocus_disk_u{};
    aleph::math::Vec3 defocus_disk_v{};
    bool              has_defocus{false};
};

inline Camera make_camera(aleph::math::Vec3 lookfrom, aleph::math::Vec3 lookat,
                           aleph::math::Vec3 vup,
                           aleph::math::f32 vfov_deg,
                           int image_width, int image_height,
                           aleph::math::f32 defocus_angle_deg,
                           aleph::math::f32 focus_dist) noexcept {
    Camera c{};
    c.center = lookfrom;
    constexpr aleph::math::f32 pi_f = 3.14159265358979323846f;
    const aleph::math::f32 theta = vfov_deg * (pi_f / 180.0f);
    const aleph::math::f32 h     = std::tan(theta * 0.5f);
    const aleph::math::f32 vp_h  = 2.0f * h * focus_dist;
    const aleph::math::f32 vp_w  = vp_h *
        static_cast<aleph::math::f32>(image_width) / static_cast<aleph::math::f32>(image_height);

    const aleph::math::Vec3 w = aleph::math::normalize(lookfrom - lookat);
    const aleph::math::Vec3 u = aleph::math::normalize(aleph::math::cross(vup, w));
    const aleph::math::Vec3 v = aleph::math::cross(w, u);

    const aleph::math::Vec3 vp_u = u *  vp_w;
    const aleph::math::Vec3 vp_v = -v * vp_h;
    c.pixel_delta_u = vp_u * (1.0f / static_cast<aleph::math::f32>(image_width));
    c.pixel_delta_v = vp_v * (1.0f / static_cast<aleph::math::f32>(image_height));

    const aleph::math::Vec3 vp_upper_left =
        lookfrom - w * focus_dist - vp_u * 0.5f - vp_v * 0.5f;
    c.pixel00_loc = vp_upper_left + (c.pixel_delta_u + c.pixel_delta_v) * 0.5f;

    c.has_defocus = defocus_angle_deg > 0.0f;
    if (c.has_defocus) {
        const aleph::math::f32 defocus_rad =
            focus_dist * std::tan(defocus_angle_deg * (pi_f / 180.0f) * 0.5f);
        c.defocus_disk_u = u * defocus_rad;
        c.defocus_disk_v = v * defocus_rad;
    }
    return c;
}

inline aleph::math::Ray camera_get_ray(const Camera& c, int px, int py,
                                        Pcg32& rng) noexcept {
    const aleph::math::f32 du = rng.float01() - 0.5f;
    const aleph::math::f32 dv = rng.float01() - 0.5f;
    const aleph::math::Vec3 sample =
        c.pixel00_loc
        + c.pixel_delta_u * (static_cast<aleph::math::f32>(px) + du)
        + c.pixel_delta_v * (static_cast<aleph::math::f32>(py) + dv);

    aleph::math::Vec3 origin = c.center;
    if (c.has_defocus) {
        // Rejection sample in unit disk.
        aleph::math::Vec3 disk{};
        for (;;) {
            const aleph::math::f32 x = rng.float01() * 2.0f - 1.0f;
            const aleph::math::f32 y = rng.float01() * 2.0f - 1.0f;
            if (x*x + y*y < 1.0f) { disk = aleph::math::Vec3{x, y, 0.0f}; break; }
        }
        origin = c.center + c.defocus_disk_u * disk.x + c.defocus_disk_v * disk.y;
    }
    return aleph::math::Ray{origin, sample - origin};
}

}  // namespace aleph::render::common
