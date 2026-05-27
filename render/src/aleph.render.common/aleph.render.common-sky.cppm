export module aleph.render.common:sky;

import aleph.math;

export namespace aleph::render::common {

struct Sky {
    aleph::math::Vec3 low;
    aleph::math::Vec3 high;
};

// Gradient sample. `unit_dir` must be unit-length.
inline aleph::math::Vec3 sky_sample(const Sky& s, aleph::math::Vec3 unit_dir) noexcept {
    const aleph::math::f32 a = 0.5f * (unit_dir.y + 1.0f);
    return aleph::math::lerp(s.low, s.high, a);
}

}  // namespace aleph::render::common
