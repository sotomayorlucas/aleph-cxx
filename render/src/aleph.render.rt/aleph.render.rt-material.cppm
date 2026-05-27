module;
#include <cmath>
#include <optional>
#include <cstdint>

export module aleph.render.rt:material;

import aleph.math;
import aleph.scene;
import aleph.render.common;
import :sampling;

export namespace aleph::render::rt {

struct ScatterResult {
    aleph::math::Ray  scattered;
    aleph::math::Vec3 attenuation;
};

[[nodiscard]] inline aleph::math::Vec3
sample_textured_albedo(const aleph::scene::Scene& s,
                        std::uint32_t mat_idx,
                        aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const std::uint32_t tex_id  = s.tex_lamb.tex_id[mat_idx];
    const aleph::math::Vec2 uvs = s.tex_lamb.uv_scale[mat_idx];
    // Image type lives in aleph::io. Use the io module's bilinear sampler if it
    // exists; otherwise a simple stub that returns gray.
    // For Phase 2 simplicity, return a fixed gray here. (Texture sampling can
    // be wired up in apps/aleph_rt when we set up the 'earth' scene.)
    (void)s; (void)mat_idx; (void)u; (void)v; (void)tex_id; (void)uvs;
    return aleph::math::Vec3{0.5f, 0.5f, 0.5f};
}

inline aleph::math::f32 schlick_reflectance(aleph::math::f32 cosine,
                                              aleph::math::f32 ref_idx) noexcept {
    aleph::math::f32 r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    const aleph::math::f32 oc = 1.0f - cosine;
    return r0 + (1.0f - r0) * oc * oc * oc * oc * oc;
}

[[nodiscard]] inline std::optional<ScatterResult>
scatter(const aleph::scene::Scene& s,
        aleph::math::Ray in,
        const aleph::scene::HitRecord& rec,
        aleph::render::common::Pcg32& rng) noexcept {
    const auto& m = rec.mat;
    switch (m.kind) {
        case aleph::scene::MaterialKind::Lambertian: {
            aleph::math::Vec3 dir = rec.normal + rng_unit_vec3(rng);
            if (aleph::math::near_zero(dir)) dir = rec.normal;
            return ScatterResult{ aleph::math::Ray{rec.p, dir}, s.lamb.albedo[m.idx] };
        }
        case aleph::scene::MaterialKind::Metal: {
            const aleph::math::Vec3 unit      = aleph::math::normalize(in.dir);
            const aleph::math::Vec3 reflected = aleph::math::reflect(unit, rec.normal);
            const aleph::math::Vec3 scat = reflected + rng_in_unit_sphere(rng) * s.metal.fuzz[m.idx];
            if (aleph::math::dot(scat, rec.normal) <= 0.0f) return std::nullopt;
            return ScatterResult{ aleph::math::Ray{rec.p, scat}, s.metal.albedo[m.idx] };
        }
        case aleph::scene::MaterialKind::Dielectric: {
            const aleph::math::f32 ior = s.diel.ior[m.idx];
            const aleph::math::Vec3 unit = aleph::math::normalize(in.dir);
            const aleph::math::f32 ri = rec.front_face ? (1.0f / ior) : ior;
            aleph::math::f32 cos_t = aleph::math::dot(-unit, rec.normal);
            if (cos_t > 1.0f) cos_t = 1.0f;
            const aleph::math::f32 sin_t = std::sqrt(1.0f - cos_t*cos_t);
            const bool tir = (ri * sin_t) > 1.0f;
            const bool refl = tir || (schlick_reflectance(cos_t, ri) > rng.float01());
            const aleph::math::Vec3 dir = refl ? aleph::math::reflect(unit, rec.normal)
                                                : aleph::math::refract(unit, rec.normal, ri);
            return ScatterResult{ aleph::math::Ray{rec.p, dir}, aleph::math::Vec3{1, 1, 1} };
        }
        case aleph::scene::MaterialKind::Emissive:
            return std::nullopt;
        case aleph::scene::MaterialKind::TexturedLambertian: {
            aleph::math::Vec3 dir = rec.normal + rng_unit_vec3(rng);
            if (aleph::math::near_zero(dir)) dir = rec.normal;
            return ScatterResult{ aleph::math::Ray{rec.p, dir},
                                  sample_textured_albedo(s, m.idx, rec.u, rec.v) };
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline aleph::math::Vec3
emitted(const aleph::scene::Scene& s, aleph::scene::MaterialHandle m) noexcept {
    if (m.kind == aleph::scene::MaterialKind::Emissive) return s.emis.emit[m.idx];
    return aleph::math::Vec3{};
}

}  // namespace aleph::render::rt
