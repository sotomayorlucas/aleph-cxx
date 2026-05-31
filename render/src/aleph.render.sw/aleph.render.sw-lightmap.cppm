module;
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <span>
#include <array>

export module aleph.render.sw:lightmap;

import aleph.math;
import :scene_rt;

export namespace aleph::render::sw {

[[nodiscard]] inline aleph::math::u32
lightmap_sample_bilinear(const Lightmap& lm, aleph::math::f32 u, aleph::math::f32 v) noexcept {
    aleph::math::f32 fu = (u - lm.u_min) / (lm.u_max - lm.u_min)
                          * static_cast<aleph::math::f32>(lm.w - 1);
    aleph::math::f32 fv = (v - lm.v_min) / (lm.v_max - lm.v_min)
                          * static_cast<aleph::math::f32>(lm.h - 1);
    fu = std::max(0.0f, fu);
    fv = std::max(0.0f, fv);
    const aleph::math::f32 max_u = static_cast<aleph::math::f32>(lm.w - 1) - 1e-4f;
    const aleph::math::f32 max_v = static_cast<aleph::math::f32>(lm.h - 1) - 1e-4f;
    if (fu > max_u) fu = max_u;
    if (fv > max_v) fv = max_v;
    const int iu = static_cast<int>(std::floor(fu));
    const int iv = static_cast<int>(std::floor(fv));
    const aleph::math::f32 fx = fu - static_cast<aleph::math::f32>(iu);
    const aleph::math::f32 fy = fv - static_cast<aleph::math::f32>(iv);
    const aleph::math::u32 c00 = lm.texels[iv      * lm.w + iu    ];
    const aleph::math::u32 c10 = lm.texels[iv      * lm.w + iu + 1];
    const aleph::math::u32 c01 = lm.texels[(iv+1)  * lm.w + iu    ];
    const aleph::math::u32 c11 = lm.texels[(iv+1)  * lm.w + iu + 1];
    const aleph::math::f32 w00 = (1.0f - fx) * (1.0f - fy);
    const aleph::math::f32 w10 = fx          * (1.0f - fy);
    const aleph::math::f32 w01 = (1.0f - fx) * fy;
    const aleph::math::f32 w11 = fx          * fy;
    auto mix = [&](aleph::math::u32 shift) {
        return static_cast<aleph::math::u32>(
            static_cast<aleph::math::f32>((c00 >> shift) & 0xFFu) * w00 +
            static_cast<aleph::math::f32>((c10 >> shift) & 0xFFu) * w10 +
            static_cast<aleph::math::f32>((c01 >> shift) & 0xFFu) * w01 +
            static_cast<aleph::math::f32>((c11 >> shift) & 0xFFu) * w11);
    };
    return 0xFF000000u | (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

namespace detail {

inline aleph::math::Vec3 quad_interp(const std::array<aleph::math::Vec3, 4>& verts,
                                      aleph::math::f32 u, aleph::math::f32 v) noexcept {
    const aleph::math::Vec3 bottom = verts[0] * (1.0f - u) + verts[1] * u;
    const aleph::math::Vec3 top    = verts[3] * (1.0f - u) + verts[2] * u;
    return bottom * (1.0f - v) + top * v;
}

inline bool quad_blocks(const Face& face, aleph::math::Vec3 origin,
                         aleph::math::Vec3 dir, aleph::math::f32 t_max) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 n  = aleph::math::cross(e1, e2);
    const aleph::math::f32 denom = aleph::math::dot(n, dir);
    if (std::abs(denom) < 1e-8f) return false;
    const aleph::math::f32 D = aleph::math::dot(n, face.verts[0]);
    const aleph::math::f32 t = (D - aleph::math::dot(n, origin)) / denom;
    if (t <= 0.001f || t >= t_max) return false;
    const aleph::math::Vec3 P = origin + dir * t;
    const aleph::math::Vec3 local = P - face.verts[0];
    const aleph::math::f32 a = aleph::math::dot(local, e1) / aleph::math::dot(e1, e1);
    const aleph::math::f32 b = aleph::math::dot(local, e2) / aleph::math::dot(e2, e2);
    return (a >= 0.0f && a <= 1.0f && b >= 0.0f && b <= 1.0f);
}

}  // namespace detail

inline void bake_lightmap_face(Lightmap& lm,
                                 const Face& face,
                                 std::span<const Face> all_faces,
                                 int skip_idx,
                                 aleph::math::Vec3 light_pos,
                                 aleph::math::f32 intensity,
                                 aleph::math::f32 ambient) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 normal = aleph::math::normalize(aleph::math::cross(e1, e2));
    aleph::math::f32 u_min = face.uvs[0].x, u_max = face.uvs[0].x;
    aleph::math::f32 v_min = face.uvs[0].y, v_max = face.uvs[0].y;
    for (std::size_t i = 1; i < 4; ++i) {
        u_min = std::min(u_min, face.uvs[i].x);
        u_max = std::max(u_max, face.uvs[i].x);
        v_min = std::min(v_min, face.uvs[i].y);
        v_max = std::max(v_max, face.uvs[i].y);
    }
    lm.u_min = u_min; lm.u_max = u_max; lm.v_min = v_min; lm.v_max = v_max;
    const int n_faces = static_cast<int>(all_faces.size());
    for (int j = 0; j < lm.h; ++j) {
        for (int i = 0; i < lm.w; ++i) {
            const aleph::math::f32 pu = (static_cast<aleph::math::f32>(i) + 0.5f)
                                         / static_cast<aleph::math::f32>(lm.w);
            const aleph::math::f32 pv = (static_cast<aleph::math::f32>(j) + 0.5f)
                                         / static_cast<aleph::math::f32>(lm.h);
            const aleph::math::Vec3 P = detail::quad_interp(face.verts, pu, pv);
            const aleph::math::Vec3 P_off = P + normal * 0.001f;
            const aleph::math::Vec3 to_L = light_pos - P_off;
            const aleph::math::f32 dist_sq = aleph::math::length_sq(to_L);
            const aleph::math::f32 dist = std::sqrt(dist_sq);
            const aleph::math::Vec3 L_dir = to_L * (1.0f / dist);
            const aleph::math::f32 cos_theta = aleph::math::dot(normal, L_dir);
            aleph::math::f32 lit = ambient;
            if (cos_theta > 0.0f) {
                bool blocked = false;
                for (int k = 0; k < n_faces; ++k) {
                    if (k == skip_idx) continue;
                    if (detail::quad_blocks(all_faces[static_cast<std::size_t>(k)], P_off, to_L, 1.0f)) {
                        blocked = true; break;
                    }
                }
                if (!blocked) lit += intensity * cos_theta / dist_sq;
            }
            lit = std::clamp(lit, 0.0f, 1.0f);
            const aleph::math::u8 b = static_cast<aleph::math::u8>(lit * 255.0f);
            lm.texels[j * lm.w + i] = 0xFF000000u
                                      | (static_cast<aleph::math::u32>(b) << 16)
                                      | (static_cast<aleph::math::u32>(b) <<  8)
                                      |  static_cast<aleph::math::u32>(b);
        }
    }
}

inline void bake_lightmaps(SceneRT& sr,
                            aleph::math::Vec3 light_pos,
                            aleph::math::f32 intensity,
                            aleph::math::f32 ambient) noexcept {
    const int n = static_cast<int>(sr.faces.size());
    for (int i = 0; i < n; ++i) {
        const aleph::math::u32 lmid = sr.faces[static_cast<std::size_t>(i)].lightmap_id;
        if (lmid == 0xFFFFFFFFu) continue;
        bake_lightmap_face(sr.lightmaps[lmid], sr.faces[static_cast<std::size_t>(i)],
                            std::span<const Face>{sr.faces}, i,
                            light_pos, intensity, ambient);
    }
}

}  // namespace aleph::render::sw
