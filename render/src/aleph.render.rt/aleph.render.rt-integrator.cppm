module;
#include <cmath>
#include <limits>
#include <cstdint>

export module aleph.render.rt:integrator;

import aleph.math;
import aleph.scene;
import aleph.render.common;
import :sampling;
import :material;

export namespace aleph::render::rt {

namespace detail {

inline aleph::math::Vec3 direct_light_quad(
    const aleph::scene::Scene& s,
    aleph::scene::Handle32 light_h,
    aleph::math::Vec3 hit_p, aleph::math::Vec3 hit_normal,
    aleph::math::Vec3 surf_albedo,
    aleph::render::common::Pcg32& rng) noexcept
{
    const std::uint32_t li = light_h.index();
    const aleph::math::f32 ra = rng.float01();
    const aleph::math::f32 rb = rng.float01();
    const aleph::math::Vec3 Q{s.quads.Qx[li], s.quads.Qy[li], s.quads.Qz[li]};
    const aleph::math::Vec3 u_edge{s.quads.ux[li], s.quads.uy[li], s.quads.uz[li]};
    const aleph::math::Vec3 v_edge{s.quads.vx[li], s.quads.vy[li], s.quads.vz[li]};
    const aleph::math::Vec3 P = Q + u_edge * ra + v_edge * rb;
    const aleph::math::Vec3 to_light = P - hit_p;
    const aleph::math::f32 dist_sq = aleph::math::length_sq(to_light);
    if (dist_sq < 1e-6f) return aleph::math::Vec3{};
    const aleph::math::f32 dist = std::sqrt(dist_sq);
    const aleph::math::f32 cos_theta = aleph::math::dot(hit_normal, to_light) / dist;
    if (cos_theta <= 0.0f) return aleph::math::Vec3{};
    const aleph::math::Vec3 n_light{s.quads.nx[li], s.quads.ny[li], s.quads.nz[li]};
    const aleph::math::f32 cos_alpha = -aleph::math::dot(n_light, to_light) / dist;
    if (cos_alpha <= 0.0f) return aleph::math::Vec3{};
    auto srec = aleph::scene::hit(s, aleph::math::Ray{hit_p, to_light}, 0.001f, 1.001f);
    if (!srec || srec->mat.kind != aleph::scene::MaterialKind::Emissive)
        return aleph::math::Vec3{};
    const aleph::math::f32 area = aleph::math::length(aleph::math::cross(u_edge, v_edge));
    const aleph::math::Vec3 emit = s.emis.emit[srec->mat.idx];
    const aleph::math::f32 factor = cos_theta * cos_alpha * area /
                                    (dist_sq * aleph::math::pi_f);
    return aleph::math::Vec3{
        surf_albedo.x * emit.x, surf_albedo.y * emit.y, surf_albedo.z * emit.z
    } * factor;
}

}  // namespace detail

[[nodiscard]] inline aleph::math::Vec3
ray_color(const aleph::scene::Scene& scene, aleph::math::Ray r, int depth,
           aleph::render::common::Sky sky, bool include_emission,
           aleph::render::common::Pcg32& rng) noexcept {
    if (depth <= 0) return aleph::math::Vec3{};

    auto rec_opt = aleph::scene::hit(scene, r, 0.001f,
                                       std::numeric_limits<aleph::math::f32>::infinity());
    if (!rec_opt) {
        const aleph::math::Vec3 unit = aleph::math::normalize(r.dir);
        return aleph::render::common::sky_sample(sky, unit);
    }
    const auto& rec = *rec_opt;
    const aleph::math::Vec3 emit_v = include_emission ? emitted(scene, rec.mat)
                                                       : aleph::math::Vec3{};
    if (rec.mat.kind == aleph::scene::MaterialKind::Emissive) return emit_v;

    aleph::math::Vec3 direct{};
    const bool is_diffuse = rec.mat.kind == aleph::scene::MaterialKind::Lambertian
                          || rec.mat.kind == aleph::scene::MaterialKind::TexturedLambertian;
    if (is_diffuse && !scene.lights.empty()) {
        const aleph::math::Vec3 surf_albedo =
            (rec.mat.kind == aleph::scene::MaterialKind::Lambertian)
                ? scene.lamb.albedo[rec.mat.idx]
                : sample_textured_albedo(scene, rec.mat.idx, rec.u, rec.v);
        for (auto Lh : scene.lights) {
            if (Lh.hittable_kind() != aleph::scene::HittableKind::Quad) continue;
            direct = direct + detail::direct_light_quad(scene, Lh, rec.p, rec.normal,
                                                         surf_albedo, rng);
        }
    }

    auto scat = scatter(scene, r, rec, rng);
    if (!scat) return emit_v + direct;

    const bool nee_done = is_diffuse && !scene.lights.empty();
    const aleph::math::Vec3 indirect = ray_color(scene, scat->scattered, depth - 1,
                                                   sky, !nee_done, rng);
    return emit_v + direct
         + aleph::math::Vec3{
               scat->attenuation.x * indirect.x,
               scat->attenuation.y * indirect.y,
               scat->attenuation.z * indirect.z,
           };
}

}  // namespace aleph::render::rt
