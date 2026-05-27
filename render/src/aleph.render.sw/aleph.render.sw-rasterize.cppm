module;
#include <vector>
#include <span>
#include <array>
#include <cstdint>
#include <algorithm>

export module aleph.render.sw:rasterize;

import aleph.math;
import aleph.render.common;
import aleph.threads;
import :scene_rt;
import :clip;
import :span_buffer;
import :rast_scan;

export namespace aleph::render::sw {

namespace detail {

inline aleph::math::Vec3 face_center(const Face& f) noexcept {
    return aleph::math::Vec3{
        (f.verts[0].x + f.verts[1].x + f.verts[2].x + f.verts[3].x) * 0.25f,
        (f.verts[0].y + f.verts[1].y + f.verts[2].y + f.verts[3].y) * 0.25f,
        (f.verts[0].z + f.verts[1].z + f.verts[2].z + f.verts[3].z) * 0.25f,
    };
}

inline ScreenVert to_screen(ClipVert cv, int W, int H) noexcept {
    const aleph::math::f32 invw = 1.0f / cv.w;
    return ScreenVert{
        (cv.x * invw * 0.5f + 0.5f) * static_cast<aleph::math::f32>(W),
        (1.0f - (cv.y * invw * 0.5f + 0.5f)) * static_cast<aleph::math::f32>(H),
        cv.z * invw,
        cv.u, cv.v,
        invw,
    };
}

}  // namespace detail

inline void rasterize(const SceneRT& sr, aleph::math::Mat4 mvp,
                       aleph::render::common::Film& fb,
                       std::span<aleph::math::f32> depth,
                       aleph::threads::Pool& pool) noexcept {
    const int N = static_cast<int>(sr.faces.size());
    if (N == 0) return;

    std::vector<int> order(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) order[i] = i;
    std::vector<aleph::math::f32> dist_sq(static_cast<std::size_t>(N), 0.0f);
    for (int i = 0; i < N; ++i) {
        const aleph::math::Vec3 c = detail::face_center(sr.faces[i]);
        const aleph::math::Vec4 cp = mvp * aleph::math::Vec4{c.x, c.y, c.z, 1.0f};
        dist_sq[i] = cp.z / (cp.w > 1e-6f ? cp.w : 1e-6f);
    }
    std::sort(order.begin(), order.end(),
              [&](int a, int b){ return dist_sq[a] < dist_sq[b]; });

    std::vector<std::array<ClipVert, 4>> clip_verts(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < 4; ++k) {
            const auto& vt = sr.faces[i].verts[k];
            const auto& uv = sr.faces[i].uvs[k];
            const aleph::math::Vec4 v{vt.x, vt.y, vt.z, 1.0f};
            const aleph::math::Vec4 cp = mvp * v;
            clip_verts[i][k] = ClipVert{cp.x, cp.y, cp.z, cp.w, uv.x, uv.y};
        }
    }

    const int n_threads = std::max(1, pool.n_threads());
    pool.parallel_for(0, n_threads, [&](int worker_id) {
        SpanBuffer sb(fb.width, fb.height);
        const int y_start = (fb.height * worker_id)       / n_threads;
        const int y_end   = (fb.height * (worker_id + 1)) / n_threads;
        constexpr std::array<std::array<aleph::math::u8, 3>, 2> quad_tris{{
            {0, 1, 2}, {0, 2, 3}
        }};
        for (int oi = 0; oi < N; ++oi) {
            const int idx = order[oi];
            const Face& face = sr.faces[idx];
            const auto& cv = clip_verts[idx];
            const Lightmap* lm = (face.lightmap_id == 0xFFFFFFFFu)
                                  ? nullptr : &sr.lightmaps[face.lightmap_id];
            for (int t = 0; t < 2; ++t) {
                const ClipVert a = cv[quad_tris[t][0]];
                const ClipVert b = cv[quad_tris[t][1]];
                const ClipVert c = cv[quad_tris[t][2]];
                std::array<ClipVert, 6> clipped{};
                const int n_tris = clip_triangle_near(a, b, c, 0.1f, clipped);
                for (int k = 0; k < n_tris; ++k) {
                    const ScreenVert s0 = detail::to_screen(clipped[k*3 + 0], fb.width, fb.height);
                    const ScreenVert s1 = detail::to_screen(clipped[k*3 + 1], fb.width, fb.height);
                    const ScreenVert s2 = detail::to_screen(clipped[k*3 + 2], fb.width, fb.height);
                    rast_scan_textured(fb, depth, sb, s0, s1, s2, face.tex, lm,
                                        y_start, y_end);
                }
            }
        }
    });
}

}  // namespace aleph::render::sw
