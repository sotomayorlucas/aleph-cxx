module;
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

// Cheap primary-ray probe (SPEC §4.3): trace ONE ray through the pixel centre
// (no jitter, no defocus disk) to read the winning primitive's baked
// importance. Misses ⇒ 0. Consumes no RNG, so the per-pixel sample loop that
// follows is unaffected.
[[nodiscard]] inline aleph::math::f32
probe_importance(const aleph::scene::Scene& scene,
                 const aleph::render::common::Camera& cam,
                 int px, int py) noexcept {
    const aleph::math::Vec3 sample =
        cam.pixel00_loc
        + cam.pixel_delta_u * static_cast<aleph::math::f32>(px)
        + cam.pixel_delta_v * static_cast<aleph::math::f32>(py);
    const aleph::math::Ray r{cam.center, sample - cam.center};
    const auto rec = aleph::scene::hit(
        scene, r, 0.001f,
        std::numeric_limits<aleph::math::f32>::infinity());
    return rec ? rec->importance : 0.0f;
}

inline void render_tile(const aleph::scene::Scene& scene,
                          const aleph::render::common::Camera& cam,
                          aleph::render::common::Sky sky,
                          aleph::render::common::Film& film,
                          int tile_idx, int tiles_x,
                          const RenderOpts& opts,
                          std::vector<std::uint32_t>* sample_counts) noexcept {
    const int tx = tile_idx % tiles_x;
    const int ty = tile_idx / tiles_x;
    const int x0 = tx * opts.tile_size;
    const int y0 = ty * opts.tile_size;
    const int x1 = std::min(x0 + opts.tile_size, film.width);
    const int y1 = std::min(y0 + opts.tile_size, film.height);
    aleph::render::common::Pcg32 rng(opts.base_seed,
                                       static_cast<aleph::math::u64>(tile_idx) + aleph::math::u64{1});
    if (opts.adaptive_spp) {
        const int spp_max =
            opts.spp * (opts.max_spp_scale > 1 ? opts.max_spp_scale : 1);
        const aleph::math::f32 scale_minus_1 =
            static_cast<aleph::math::f32>(opts.max_spp_scale - 1);
        for (int j = y0; j < y1; ++j) {
            for (int i = x0; i < x1; ++i) {
                const aleph::math::f32 importance =
                    probe_importance(scene, cam, i, j);
                // spp_local = clamp(round(base_spp*(1+(max_scale-1)*imp)), 1, base*max_scale)
                const aleph::math::f32 raw =
                    static_cast<aleph::math::f32>(opts.spp)
                    * (1.0f + scale_minus_1 * importance);
                int spp_local = static_cast<int>(std::lround(raw));
                if (spp_local < 1) spp_local = 1;
                if (spp_local > spp_max) spp_local = spp_max;
                aleph::math::Vec3 accum{};
                for (int s = 0; s < spp_local; ++s) {
                    const aleph::math::Ray r =
                        aleph::render::common::camera_get_ray(cam, i, j, rng);
                    accum = accum + ray_color(scene, r, opts.max_depth, sky, true,
                                              rng, opts.grouped_nee);
                }
                const aleph::math::f32 inv_local =
                    1.0f / static_cast<aleph::math::f32>(spp_local);
                film.pixels[j * film.stride_pixels + i] = accum * inv_local;
                if (sample_counts) {
                    (*sample_counts)[static_cast<std::size_t>(j) * static_cast<std::size_t>(film.width)
                                     + static_cast<std::size_t>(i)] =
                        static_cast<std::uint32_t>(spp_local);
                }
            }
        }
        return;
    }
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
            if (sample_counts) {
                (*sample_counts)[static_cast<std::size_t>(j) * static_cast<std::size_t>(film.width)
                                 + static_cast<std::size_t>(i)] =
                    static_cast<std::uint32_t>(opts.spp);
            }
        }
    }
}

}  // namespace detail

inline void path_trace(const aleph::scene::Scene& scene,
                        const aleph::render::common::Camera& cam,
                        aleph::render::common::Sky sky,
                        aleph::render::common::Film& film,
                        aleph::threads::Pool& pool,
                        RenderOpts opts,
                        std::vector<std::uint32_t>* sample_counts = nullptr) noexcept {
    const int tiles_x = (film.width  + opts.tile_size - 1) / opts.tile_size;
    const int tiles_y = (film.height + opts.tile_size - 1) / opts.tile_size;
    const int total = tiles_x * tiles_y;
    pool.parallel_for(0, total, [&](int t) {
        detail::render_tile(scene, cam, sky, film, t, tiles_x, opts, sample_counts);
    });
}

}  // namespace aleph::render::rt
