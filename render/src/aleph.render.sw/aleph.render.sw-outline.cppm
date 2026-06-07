module;
#include <cassert>
#include <span>
#include <cstddef>

export module aleph.render.sw:outline;

import aleph.math;
import aleph.render.common;

export namespace aleph::render::sw {

// Paint `color` into `fb` at every pixel that is NOT covered but lies within
// `radius` (Chebyshev) of a covered pixel — i.e. a ring hugging the OUTSIDE of
// the silhouette. Coverage is `sel_depth[y*W + x] > 0` (the depth written by a
// rasterize pass of only the selected entity's faces into a zero-cleared
// buffer). `sel_depth` is indexed y*fb.width + x (matching rasterize's depth);
// `fb` is written at y*fb.stride_pixels + x. No allocation, no read of `fb`'s
// existing colour. Caller runs this at SSAA resolution before the downsample so
// the box filter anti-aliases the ring for free.
inline void draw_selection_outline(aleph::render::common::Film& fb,
                                   std::span<const aleph::math::f32> sel_depth,
                                   int radius,
                                   aleph::math::Vec3 color) noexcept {
    const int W = fb.width, H = fb.height;
    assert(sel_depth.size() >= static_cast<std::size_t>(W)
                                * static_cast<std::size_t>(H));
    auto covered = [&](int x, int y) noexcept -> bool {
        return sel_depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(W)
                         + static_cast<std::size_t>(x)] > 0.0f;
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (covered(x, y)) continue;             // inside the silhouette
            bool edge = false;
            for (int dy = -radius; dy <= radius && !edge; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= H) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= W) continue;
                    if (covered(nx, ny)) { edge = true; break; }
                }
            }
            if (edge) {
                fb.pixels[static_cast<std::size_t>(y)
                          * static_cast<std::size_t>(fb.stride_pixels)
                          + static_cast<std::size_t>(x)] = color;
            }
        }
    }
}

}  // namespace aleph::render::sw
