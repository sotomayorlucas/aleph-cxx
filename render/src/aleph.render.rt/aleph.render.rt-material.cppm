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

// Procedural checker levels (single source of truth: the LO byte 0x80 under the
// renderer's plain byte/255 decode). HI = argb_to_linear(0xFF), LO = 0x80/255.
inline constexpr aleph::math::f32 kCheckerHi = 1.0f;
inline constexpr aleph::math::f32 kCheckerLo = 128.0f / 255.0f;

struct ScatterResult {
    aleph::math::Ray  scattered;
    aleph::math::Vec3 attenuation;
};

[[nodiscard]] inline aleph::math::Vec3
sample_textured_albedo(const aleph::scene::Scene& s,
                        std::uint32_t mat_idx,
                        aleph::math::f32 u, aleph::math::f32 v) noexcept {
    // Analytic procedural checker. uv_scale tiles the [0,1]² param face; the
    // cell parity selects HI/LO, base color is the material albedo. Pure-f32,
    // deterministic; matches the rasterizer's tex_checker_uv (same cell fn).
    // Precondition: u,v ∈ [0,1] (hit_quad's α,β and hit_sphere's φ/2π,θ/π are
    // bounded), so floor(u·sc) stays in a small non-negative int range — the
    // cast is safe and the XOR parity is well-defined.
    const aleph::math::Vec2 sc = s.tex_lamb.uv_scale[mat_idx];
    const aleph::math::Vec3 a  = s.tex_lamb.albedo[mat_idx];
    const int cu = static_cast<int>(std::floor(u * sc.x));
    const int cv = static_cast<int>(std::floor(v * sc.y));
    return a * (((cu ^ cv) & 1) ? kCheckerHi : kCheckerLo);
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
