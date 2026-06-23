module;
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

export module aleph.render.sw:rast_scan;

import aleph.math;
import aleph.render.common;
import :clip;
import :scene_rt;
import :span_buffer;

export namespace aleph::render::sw {

namespace detail {

// Texel decode: true sRGB EOTF per channel (a 256-entry LUT read — exact and
// hot-loop cheap). The inverse of byte_from_linear's OETF, so texture bytes
// and bake-encoded lightmap bytes round-trip exactly through the present.
inline aleph::math::Vec3 argb_to_linear(aleph::math::u32 argb) noexcept {
    using aleph::render::common::srgb_decode_byte;
    const aleph::math::f32 r = srgb_decode_byte(static_cast<std::uint8_t>((argb >> 16) & 0xFFu));
    const aleph::math::f32 g = srgb_decode_byte(static_cast<std::uint8_t>((argb >>  8) & 0xFFu));
    const aleph::math::f32 b = srgb_decode_byte(static_cast<std::uint8_t>( argb        & 0xFFu));
    return aleph::math::Vec3{r, g, b};
}

}  // namespace detail

inline void rast_scan_textured(aleph::render::common::Film& fb,
                                std::span<aleph::math::f32> depth,
                                SpanBuffer& sb,
                                ScreenVert v0, ScreenVert v1, ScreenVert v2,
                                TexSampleFn tex,
                                const Lightmap* lm,
                                bool two_sided,
                                int y_clip_min, int y_clip_max) noexcept {
    const aleph::math::f32 signed_area =
        (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
    if (signed_area < 0.0f) {
        if (!two_sided) return;  // one-sided: back-face cull (CW-front winding)
        // Two-sided back face: re-wind to CW BEFORE any gradient/edge setup.
        // uv/colour/inv_w ride inside ScreenVert, so the swap carries them; the
        // swapped triangle's signed area is exactly -signed_area > 0 (nothing
        // below reads signed_area, so no recompute is needed).
        std::swap(v1, v2);
    }

    if (v0.y > v1.y) std::swap(v0, v1);
    if (v1.y > v2.y) std::swap(v1, v2);
    if (v0.y > v1.y) std::swap(v0, v1);

    const aleph::math::f32 dx10 = v1.x - v0.x, dy10 = v1.y - v0.y;
    const aleph::math::f32 dx20 = v2.x - v0.x, dy20 = v2.y - v0.y;
    const aleph::math::f32 det = dx10 * dy20 - dy10 * dx20;
    if (std::abs(det) < 1e-6f) return;
    const aleph::math::f32 inv_det = 1.0f / det;

    const aleph::math::f32 iw0 = v0.inv_w, iw1 = v1.inv_w, iw2 = v2.inv_w;
    const aleph::math::f32 uw0 = v0.u * iw0, uw1 = v1.u * iw1, uw2 = v2.u * iw2;
    const aleph::math::f32 vw0 = v0.v * iw0, vw1 = v1.v * iw1, vw2 = v2.v * iw2;

    struct Grad {
        aleph::math::f32 inv_w_dx, inv_w_dy, inv_w_origin;
        aleph::math::f32 u_w_dx,   u_w_dy,   u_w_origin;
        aleph::math::f32 v_w_dx,   v_w_dy,   v_w_origin;
        aleph::math::f32 r_dx, r_dy, r_org;   // per-vertex Gouraud colour (affine)
        aleph::math::f32 g_dx, g_dy, g_org;
        aleph::math::f32 b_dx, b_dy, b_org;
    } g{};

    auto compute_grad = [&](aleph::math::f32 a0, aleph::math::f32 a1, aleph::math::f32 a2,
                              aleph::math::f32& dx, aleph::math::f32& dy, aleph::math::f32& org) {
        const aleph::math::f32 d10 = a1 - a0;
        const aleph::math::f32 d20 = a2 - a0;
        dx  = (d10 * dy20 - d20 * dy10) * inv_det;
        dy  = (d20 * dx10 - d10 * dx20) * inv_det;
        org = a0 - dx * v0.x - dy * v0.y;
    };
    compute_grad(iw0, iw1, iw2, g.inv_w_dx, g.inv_w_dy, g.inv_w_origin);
    compute_grad(uw0, uw1, uw2, g.u_w_dx,   g.u_w_dy,   g.u_w_origin);
    compute_grad(vw0, vw1, vw2, g.v_w_dx,   g.v_w_dy,   g.v_w_origin);
    // Gouraud colour: interpolate the per-vertex tint affinely (screen-linear) in
    // each channel. Equal verts => flat; distinct verts => smooth (no facets).
    compute_grad(v0.col.x, v1.col.x, v2.col.x, g.r_dx, g.r_dy, g.r_org);
    compute_grad(v0.col.y, v1.col.y, v2.col.y, g.g_dx, g.g_dy, g.g_org);
    compute_grad(v0.col.z, v1.col.z, v2.col.z, g.b_dx, g.b_dy, g.b_org);
    // Depth = 1/w (view-linear), which is exactly affine in screen space and the
    // same quantity we interpolate for perspective correction. NEARER fragments
    // have LARGER 1/w. Per-pixel z-test resolves occlusion the face-center
    // painter sort cannot (e.g. a huge floor quad whose centroid ties the
    // sphere's depth); 1/w avoids the near-plane precision crunch of NDC z, where
    // everything past a few units collapses to ~1.0 and z-fights.

    const int y_top = static_cast<int>(std::ceil(v0.y));
    const int y_bot = static_cast<int>(std::ceil(v2.y));
    const int y_mid = static_cast<int>(std::ceil(v1.y));
    const aleph::math::f32 dy_long   = v2.y - v0.y;
    const aleph::math::f32 dxdy_long  = (dy_long  > 1e-6f) ? (v2.x - v0.x) / dy_long  : 0.0f;
    const aleph::math::f32 dy_upper  = v1.y - v0.y;
    const aleph::math::f32 dxdy_upper = (dy_upper > 1e-6f) ? (v1.x - v0.x) / dy_upper : 0.0f;
    const aleph::math::f32 dy_lower  = v2.y - v1.y;
    const aleph::math::f32 dxdy_lower = (dy_lower > 1e-6f) ? (v2.x - v1.x) / dy_lower : 0.0f;

    const int y_start = std::max({y_top, y_clip_min, 0});
    const int y_end   = std::min({y_bot, y_clip_max, fb.height});
    constexpr int SUBSPAN = 16;

    for (int y = y_start; y < y_end; ++y) {
        const aleph::math::f32 yc = static_cast<aleph::math::f32>(y) + 0.5f;
        const aleph::math::f32 x_long = v0.x + (yc - v0.y) * dxdy_long;
        aleph::math::f32 x_short;
        if (y < y_mid && dy_upper > 1e-6f) x_short = v0.x + (yc - v0.y) * dxdy_upper;
        else if (dy_lower > 1e-6f)         x_short = v1.x + (yc - v1.y) * dxdy_lower;
        else continue;

        const aleph::math::f32 xl_f = std::min(x_long, x_short);
        const aleph::math::f32 xr_f = std::max(x_long, x_short);
        const int xl = static_cast<int>(std::ceil(xl_f));
        const int xr = static_cast<int>(std::ceil(xr_f));
        if (xl >= xr) continue;

        sb.emit(y, xl, xr, [&](int yy, int x0, int x1) {
            const aleph::math::f32 xs = static_cast<aleph::math::f32>(x0) + 0.5f;
            const aleph::math::f32 ys = static_cast<aleph::math::f32>(yy) + 0.5f;
            aleph::math::f32 inv_w_acc = g.inv_w_origin + xs * g.inv_w_dx + ys * g.inv_w_dy;
            aleph::math::f32 u_w_acc   = g.u_w_origin   + xs * g.u_w_dx   + ys * g.u_w_dy;
            aleph::math::f32 v_w_acc   = g.v_w_origin   + xs * g.v_w_dx   + ys * g.v_w_dy;
            aleph::math::f32 tu_left = u_w_acc / inv_w_acc;
            aleph::math::f32 tv_left = v_w_acc / inv_w_acc;

            int x = x0;
            while (x < x1) {
                int x_next = std::min(x + SUBSPAN, x1);
                const int len = x_next - x;
                const aleph::math::f32 len_f = static_cast<aleph::math::f32>(len);
                inv_w_acc += g.inv_w_dx * len_f;
                u_w_acc   += g.u_w_dx   * len_f;
                v_w_acc   += g.v_w_dx   * len_f;
                const aleph::math::f32 w_right = 1.0f / inv_w_acc;
                const aleph::math::f32 tu_right = u_w_acc * w_right;
                const aleph::math::f32 tv_right = v_w_acc * w_right;
                const aleph::math::f32 inv_len = 1.0f / len_f;
                const aleph::math::f32 du = (tu_right - tu_left) * inv_len;
                const aleph::math::f32 dv = (tv_right - tv_left) * inv_len;

                aleph::math::f32 tu = tu_left;
                aleph::math::f32 tv = tv_left;
                aleph::math::f32 zw_pix =
                    g.inv_w_origin + (static_cast<aleph::math::f32>(x) + 0.5f) * g.inv_w_dx
                    + ys * g.inv_w_dy;
                for (int i = 0; i < len; ++i) {
                    const int px = x + i;
                    const std::size_t di =
                        static_cast<std::size_t>(yy) * static_cast<std::size_t>(fb.width)
                        + static_cast<std::size_t>(px);
                    // 1/w depth: nearer fragment has the LARGER value.
                    if (zw_pix <= depth[di]) { zw_pix += g.inv_w_dx; tu += du; tv += dv; continue; }
                    const aleph::math::f32 pxc = static_cast<aleph::math::f32>(px) + 0.5f;
                    const aleph::math::Vec3 tint{
                        g.r_org + pxc * g.r_dx + ys * g.r_dy,
                        g.g_org + pxc * g.g_dx + ys * g.g_dy,
                        g.b_org + pxc * g.b_dx + ys * g.b_dy};
                    const aleph::math::u32 base = tex(tu, tv);
                    aleph::math::Vec3 color = detail::argb_to_linear(base);
                    if (lm) {
                        const aleph::math::f32 fu = (tu - lm->u_min) / (lm->u_max - lm->u_min)
                                                    * static_cast<aleph::math::f32>(lm->w - 1);
                        const aleph::math::f32 fv = (tv - lm->v_min) / (lm->v_max - lm->v_min)
                                                    * static_cast<aleph::math::f32>(lm->h - 1);
                        const int iu = std::clamp(static_cast<int>(fu), 0, lm->w - 1);
                        const int iv = std::clamp(static_cast<int>(fv), 0, lm->h - 1);
                        const aleph::math::u32 mod = lm->texels[iv * lm->w + iu];
                        const aleph::math::Vec3 mod_color = detail::argb_to_linear(mod);
                        color = aleph::math::Vec3{
                            color.x * mod_color.x, color.y * mod_color.y, color.z * mod_color.z};
                    }
                    color = aleph::math::Vec3{color.x * tint.x,
                                              color.y * tint.y,
                                              color.z * tint.z};
                    depth[di] = zw_pix;
                    fb.pixels[yy * fb.stride_pixels + px] = color;
                    zw_pix += g.inv_w_dx; tu += du; tv += dv;
                }
                tu_left = tu_right;
                tv_left = tv_right;
                x = x_next;
            }
        });
    }
}

}  // namespace aleph::render::sw
