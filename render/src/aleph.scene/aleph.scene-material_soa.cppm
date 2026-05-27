module;
#include <cstdint>
#include <vector>

export module aleph.scene:material_soa;

import aleph.math;

export namespace aleph::scene {

struct LambertianSoA { std::vector<aleph::math::Vec3> albedo; };
inline std::uint32_t lambertian_append(LambertianSoA& s, aleph::math::Vec3 albedo) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.albedo.size());
    s.albedo.push_back(albedo);
    return idx;
}

struct MetalSoA {
    std::vector<aleph::math::Vec3> albedo;
    std::vector<aleph::math::f32>  fuzz;
};
inline std::uint32_t metal_append(MetalSoA& s, aleph::math::Vec3 albedo, aleph::math::f32 fuzz) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.albedo.size());
    s.albedo.push_back(albedo);
    s.fuzz.push_back(fuzz);
    return idx;
}

struct DielectricSoA { std::vector<aleph::math::f32> ior; };
inline std::uint32_t dielectric_append(DielectricSoA& s, aleph::math::f32 ior) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.ior.size());
    s.ior.push_back(ior);
    return idx;
}

struct EmissiveSoA { std::vector<aleph::math::Vec3> emit; };
inline std::uint32_t emissive_append(EmissiveSoA& s, aleph::math::Vec3 emit) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.emit.size());
    s.emit.push_back(emit);
    return idx;
}

struct TexturedLambertianSoA {
    std::vector<std::uint32_t>     tex_id;
    std::vector<aleph::math::Vec2> uv_scale;
};
inline std::uint32_t textured_lambertian_append(TexturedLambertianSoA& s,
                                                  std::uint32_t tex_id,
                                                  aleph::math::Vec2 uv_scale) noexcept {
    const std::uint32_t idx = static_cast<std::uint32_t>(s.tex_id.size());
    s.tex_id.push_back(tex_id);
    s.uv_scale.push_back(uv_scale);
    return idx;
}

}  // namespace aleph::scene
