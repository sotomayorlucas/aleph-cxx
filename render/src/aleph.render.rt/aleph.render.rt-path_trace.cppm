module;
#include <atomic>
#include <cstdint>
#include <algorithm>

export module aleph.render.rt:path_trace;

import aleph.math;
import aleph.scene;
import aleph.render.common;
import aleph.threads;
import :integrator;

export namespace aleph::render::rt {

struct RenderOpts {
    int spp{16};
    int max_depth{8};
    aleph::math::u64 base_seed{42};
    int tile_size{32};
    // Opt-in group-stratified NEE (SPEC §4.3). Default false preserves the
    // existing all-lights-sum path byte-for-byte.
    bool grouped_nee{false};
    // Opt-in adaptive spp (Phase 5.x-b SPEC §4.3). Default false ⇒ the existing
    // uniform-spp loop, byte-identical. W3 fills the adaptive loop.
    bool adaptive_spp{false};
    int  max_spp_scale{4};
};

namespace detail {

inline void render_tile(const aleph::scene::Scene& scene,
                          const aleph::render::common::Camera& cam,
                          aleph::render::common::Sky sky,
                          aleph::render::common::Film& film,
                          int tile_idx, int tiles_x,
                          const RenderOpts& opts) noexcept {
    const int tx = tile_idx % tiles_x;
    const int ty = tile_idx / tiles_x;
    const int x0 = tx * opts.tile_size;
    const int y0 = ty * opts.tile_size;
    const int x1 = std::min(x0 + opts.tile_size, film.width);
    const int y1 = std::min(y0 + opts.tile_size, film.height);
    aleph::render::common::Pcg32 rng(opts.base_seed,
                                       static_cast<aleph::math::u64>(tile_idx) + aleph::math::u64{1});
    const aleph::math::f32 inv_spp = 1.0f / static_cast<aleph::math::f32>(opts.spp);
    for (int j = y0; j < y1; ++j) {
        for (int i = x0; i < x1; ++i) {
            aleph::math::Vec3 accum{};
            for (int s = 0; s < opts.spp; ++s) {
                const aleph::math::Ray r = aleph::render::common::camera_get_ray(cam, i, j, rng);
                accum = accum + ray_color(scene, r, opts.max_depth, sky, true, rng,
                                          opts.grouped_nee);
            }
            film.pixels[j * film.stride_pixels + i] = accum * inv_spp;
        }
    }
}

}  // namespace detail

inline void path_trace(const aleph::scene::Scene& scene,
                        const aleph::render::common::Camera& cam,
                        aleph::render::common::Sky sky,
                        aleph::render::common::Film& film,
                        aleph::threads::Pool& pool,
                        RenderOpts opts) noexcept {
    const int tiles_x = (film.width  + opts.tile_size - 1) / opts.tile_size;
    const int tiles_y = (film.height + opts.tile_size - 1) / opts.tile_size;
    const int total = tiles_x * tiles_y;
    pool.parallel_for(0, total, [&](int t) {
        detail::render_tile(scene, cam, sky, film, t, tiles_x, opts);
    });
}

}  // namespace aleph::render::rt
