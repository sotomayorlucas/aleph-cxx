export module aleph.render.rt:sampling;

import aleph.math;
import aleph.render.common;     // for Pcg32

export namespace aleph::render::rt {

inline aleph::math::Vec3 rng_in_unit_sphere(aleph::render::common::Pcg32& r) noexcept {
    for (;;) {
        const aleph::math::Vec3 p{
            r.float01() * 2.0f - 1.0f,
            r.float01() * 2.0f - 1.0f,
            r.float01() * 2.0f - 1.0f,
        };
        if (aleph::math::length_sq(p) < 1.0f) return p;
    }
}

inline aleph::math::Vec3 rng_unit_vec3(aleph::render::common::Pcg32& r) noexcept {
    return aleph::math::normalize(rng_in_unit_sphere(r));
}

}  // namespace aleph::render::rt
