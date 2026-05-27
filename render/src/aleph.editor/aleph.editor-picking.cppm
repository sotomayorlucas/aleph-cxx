module;
#include <cmath>
#include <limits>

export module aleph.editor:picking;

import aleph.math;
import aleph.render.sw;

export namespace aleph::editor {

namespace detail {

inline aleph::math::f32 face_intersect(const aleph::render::sw::Face& face,
                                         aleph::math::Vec3 orig,
                                         aleph::math::Vec3 dir) noexcept {
    const aleph::math::Vec3 e1 = face.verts[1] - face.verts[0];
    const aleph::math::Vec3 e2 = face.verts[3] - face.verts[0];
    const aleph::math::Vec3 n  = aleph::math::cross(e1, e2);
    const aleph::math::f32 denom = aleph::math::dot(n, dir);
    if (std::abs(denom) < 1e-8f) return -1.0f;
    const aleph::math::f32 D = aleph::math::dot(n, face.verts[0]);
    const aleph::math::f32 t = (D - aleph::math::dot(n, orig)) / denom;
    if (t < 0.001f) return -1.0f;
    const aleph::math::Vec3 P = orig + dir * t;
    const aleph::math::Vec3 local = P - face.verts[0];
    const aleph::math::f32 a = aleph::math::dot(local, e1) / aleph::math::dot(e1, e1);
    const aleph::math::f32 b = aleph::math::dot(local, e2) / aleph::math::dot(e2, e2);
    if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f) return -1.0f;
    return t;
}

}  // namespace detail

inline int pick_face(const aleph::render::sw::SceneRT& sr, int sx, int sy,
                      aleph::math::Vec3 eye, aleph::math::Vec3 target,
                      aleph::math::Vec3 world_up,
                      aleph::math::f32 vfov_rad, aleph::math::f32 aspect,
                      int win_w, int win_h) noexcept {
    const aleph::math::f32 ndc_x = 2.0f * static_cast<aleph::math::f32>(sx) /
                                    static_cast<aleph::math::f32>(win_w) - 1.0f;
    const aleph::math::f32 ndc_y = 1.0f - 2.0f * static_cast<aleph::math::f32>(sy) /
                                    static_cast<aleph::math::f32>(win_h);
    const aleph::math::f32 t_y = std::tan(vfov_rad * 0.5f);
    const aleph::math::f32 t_x = t_y * aspect;
    const aleph::math::Vec3 fwd   = aleph::math::normalize(target - eye);
    const aleph::math::Vec3 right = aleph::math::normalize(aleph::math::cross(fwd, world_up));
    const aleph::math::Vec3 up    = aleph::math::normalize(aleph::math::cross(right, fwd));
    const aleph::math::Vec3 ray_dir = right * (ndc_x * t_x) + up * (ndc_y * t_y) + fwd;
    int best = -1;
    aleph::math::f32 best_t = std::numeric_limits<aleph::math::f32>::infinity();
    for (int i = 0; i < static_cast<int>(sr.faces.size()); ++i) {
        const aleph::math::f32 t = detail::face_intersect(sr.faces[static_cast<unsigned>(i)], eye, ray_dir);
        if (t > 0.0f && t < best_t) { best_t = t; best = i; }
    }
    return best;
}

}  // namespace aleph::editor
