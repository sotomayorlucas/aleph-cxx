module;
#include <array>
#include <cstddef>

export module aleph.render.sw:clip;

import aleph.math;

export namespace aleph::render::sw {

struct ClipVert {
    aleph::math::f32 x, y, z, w;
    aleph::math::f32 u, v;
};

struct ScreenVert {
    aleph::math::f32 x, y, z;
    aleph::math::f32 u, v, inv_w;
};

namespace detail {

inline ClipVert lerp_clip(ClipVert a, ClipVert b, aleph::math::f32 t) noexcept {
    return ClipVert{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
        a.u + (b.u - a.u) * t,
        a.v + (b.v - a.v) * t,
    };
}

}  // namespace detail

inline int clip_triangle_near(ClipVert a, ClipVert b, ClipVert c,
                                aleph::math::f32 near_w,
                                std::array<ClipVert, 6>& out) noexcept {
    const std::array<ClipVert, 3> verts{a, b, c};
    const std::array<int, 3> ins{
        a.w >= near_w ? 1 : 0,
        b.w >= near_w ? 1 : 0,
        c.w >= near_w ? 1 : 0,
    };
    const int n_in = ins[0] + ins[1] + ins[2];
    if (n_in == 0) return 0;
    if (n_in == 3) { out[0] = a; out[1] = b; out[2] = c; return 1; }

    std::array<ClipVert, 4> poly{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t j = (i + 1) % 3;
        if (ins[i]) poly[n++] = verts[i];
        if (ins[i] != ins[j]) {
            const aleph::math::f32 t =
                (near_w - verts[i].w) / (verts[j].w - verts[i].w);
            poly[n] = detail::lerp_clip(verts[i], verts[j], t);
            poly[n].w = near_w;
            ++n;
        }
    }
    std::size_t n_tris = 0;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        out[n_tris * 3 + 0] = poly[0];
        out[n_tris * 3 + 1] = poly[i];
        out[n_tris * 3 + 2] = poly[i + 1];
        ++n_tris;
    }
    return static_cast<int>(n_tris);
}

}  // namespace aleph::render::sw
