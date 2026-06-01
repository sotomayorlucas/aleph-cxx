// SPEC §5 tests 3, 4, 5 — adaptive-spp (Phase 5.x-b).
//
//   (3) adaptive_alloc (mechanism): render a scene with a high-importance (~1)
//       and a low-importance (~0) region; the per-pixel sample_counts averaged
//       over the high region ≈ max_spp_scale × the low region (within
//       tolerance). Proves allocation ∝ importance.
//   (4) default_unchanged: adaptive_spp=false renders byte-identical to the
//       fixed-scene/fixed-seed baseline (and the max_spp_scale knob is inert
//       while the feature is off).
//   (5) purity: render.rt + aleph.scene reference no aleph.flow / aleph::flow.
//       Grep-asserted here over the module sources (SPEC §5.5 allows grep or
//       relying on iso_render_rt / iso_scene). The architectural law (SPEC
//       §1/§2): importance is a plain f32 array baked onto the Scene by the
//       lowering — the render and scene layers never know flow exists.
//
// Architectural note (SPEC §4.3): the renderer reads importance ONLY from the
// HitRecord, which carries the winning primitive's baked f32. This test bakes
// importance directly into the SoA `importance` arrays (plain data) rather than
// going through the lowering, keeping the render layer isolated from flow.

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::scene::Scene;

namespace {

// Centered (non-jittered) primary ray reconstructed straight from the public
// camera basis — matches camera_get_ray with du=dv=0. We use it to classify
// each pixel by the importance of the primitive its primary ray hits, so the
// classification is deterministic and decoupled from the renderer's own RNG.
[[nodiscard]] aleph::math::Ray center_ray(const aleph::render::common::Camera& c,
                                          int px, int py) noexcept {
    const aleph::math::Vec3 sample =
        c.pixel00_loc
        + c.pixel_delta_u * static_cast<aleph::math::f32>(px)
        + c.pixel_delta_v * static_cast<aleph::math::f32>(py);
    return aleph::math::Ray{c.center, sample - c.center};
}

// Two big diffuse quads facing the camera, side by side with a gap between
// them: the LEFT quad gets importance ~1, the RIGHT quad ~0. The camera looks
// straight down -z so the split is a clean vertical band, and many pixels land
// cleanly inside each region (the gap / background pixels are excluded by the
// classifier below).
struct TwoRegionScene {
    Scene                          scene;
    aleph::render::common::Camera  cam;
    aleph::render::common::Sky     sky;
    std::uint32_t                  hi_quad_idx{};
    std::uint32_t                  lo_quad_idx{};
};

[[nodiscard]] TwoRegionScene make_two_region_scene(int W, int H) {
    TwoRegionScene out;
    Scene& s = out.scene;

    const auto m_hi = aleph::scene::scene_add_lambertian(s, Vec3{0.8f, 0.2f, 0.2f});
    const auto m_lo = aleph::scene::scene_add_lambertian(s, Vec3{0.2f, 0.2f, 0.8f});

    // Left quad: spans x in [-3,-0.5], y in [-2,2], at z=0. Importance ~1.
    out.hi_quad_idx = aleph::scene::scene_add_quad(
        s, Vec3{-3.0f, -2.0f, 0.0f}, Vec3{2.5f, 0, 0}, Vec3{0, 4.0f, 0}, m_hi).index();
    // Right quad: spans x in [0.5,3], y in [-2,2], at z=0. Importance ~0.
    out.lo_quad_idx = aleph::scene::scene_add_quad(
        s, Vec3{0.5f, -2.0f, 0.0f}, Vec3{2.5f, 0, 0}, Vec3{0, 4.0f, 0}, m_lo).index();

    // Bake importance directly into the SoA store (plain f32 data, SPEC §4.1).
    s.quads.importance[out.hi_quad_idx] = 1.0f;
    s.quads.importance[out.lo_quad_idx] = 0.0f;

    out.cam = aleph::render::common::make_camera(
        Vec3{0, 0, 6}, Vec3{0, 0, 0}, Vec3{0, 1, 0}, 60.0f, W, H, 0.0f, 6.0f);
    out.sky = aleph::render::common::Sky{Vec3{0.05f, 0.05f, 0.05f},
                                         Vec3{0.10f, 0.12f, 0.16f}};
    return out;
}

// Read a module source file given a path relative to the repo root, which we
// derive from this test's __FILE__ (an absolute path .../tests/render/...).
[[nodiscard]] std::string repo_root_from_file() {
    std::string f = __FILE__;
    const std::string marker = "/tests/render/";
    const auto pos = f.rfind(marker);
    REQUIRE_MESSAGE(pos != std::string::npos,
                    "__FILE__ not under tests/render/: " << f);
    return f.substr(0, pos);
}

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.is_open(), "failed to open " << path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("adaptive_spp: sample_counts over high-importance region ≈ scale × low") {
    constexpr int W = 64;
    constexpr int H = 48;
    TwoRegionScene t = make_two_region_scene(W, H);

    alignas(16) static unsigned char scratch[64 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(t.scene, arena);

    alignas(64) static unsigned char film_buf[W * H * sizeof(Vec3) + 256];
    aleph::alloc::Arena film_arena{film_buf, sizeof(film_buf)};
    aleph::render::common::Film film =
        aleph::render::common::film_alloc(film_arena, W, H);

    aleph::threads::Pool pool(2);
    RenderOpts opts{};
    opts.spp           = 8;
    opts.max_depth     = 2;
    opts.base_seed     = 7;
    opts.tile_size     = 16;
    opts.adaptive_spp  = true;
    opts.max_spp_scale = 4;

    // Per-pixel sample-count instrumentation (SPEC §4.3).
    std::vector<std::uint32_t> sample_counts(static_cast<std::size_t>(W) * H, 0u);
    path_trace(t.scene, t.cam, t.sky, film, pool, opts, &sample_counts);

    // Classify each pixel by the importance of the primitive its centered
    // primary ray hits. Pixels that hit neither region (gap / background) are
    // excluded so the two means are clean.
    double hi_sum = 0.0, lo_sum = 0.0;
    long   hi_n = 0,     lo_n = 0;
    for (int j = 0; j < H; ++j) {
        for (int i = 0; i < W; ++i) {
            const auto rec = aleph::scene::hit(
                t.scene, center_ray(t.cam, i, j), 0.001f,
                std::numeric_limits<aleph::math::f32>::infinity());
            if (!rec) continue;
            const std::uint32_t sc = sample_counts[static_cast<std::size_t>(j) *
                                                        static_cast<std::size_t>(W) +
                                                    static_cast<std::size_t>(i)];
            if (rec->importance > 0.5f) { hi_sum += sc; ++hi_n; }
            else                        { lo_sum += sc; ++lo_n; }
        }
    }

    // Both regions must be genuinely covered or the comparison is vacuous.
    REQUIRE(hi_n > 50);
    REQUIRE(lo_n > 50);

    const double hi_mean = hi_sum / static_cast<double>(hi_n);
    const double lo_mean = lo_sum / static_cast<double>(lo_n);

    INFO("hi_mean=" << hi_mean << " lo_mean=" << lo_mean
                    << " scale=" << opts.max_spp_scale
                    << " hi_n=" << hi_n << " lo_n=" << lo_n);

    // Low region (importance ~0): spp_local = base spp.
    CHECK(lo_mean == doctest::Approx(static_cast<double>(opts.spp)).epsilon(0.05));
    // High region (importance ~1): spp_local = base spp × max_spp_scale.
    CHECK(hi_mean ==
          doctest::Approx(static_cast<double>(opts.spp) * opts.max_spp_scale)
              .epsilon(0.05));
    // The defining ratio of the mechanism: high ≈ scale × low (within tol).
    CHECK(hi_mean / lo_mean ==
          doctest::Approx(static_cast<double>(opts.max_spp_scale)).epsilon(0.05));
}

TEST_CASE("adaptive_spp: adaptive_spp=false is byte-identical to the baseline") {
    constexpr int W = 32;
    constexpr int H = 32;
    TwoRegionScene t = make_two_region_scene(W, H);

    alignas(16) static unsigned char scratch[64 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(t.scene, arena);

    // A fixed render with the feature OFF (the pre-feature uniform path).
    auto render = [&](const RenderOpts& opts) -> std::vector<Vec3> {
        alignas(64) static unsigned char film_buf[W * H * sizeof(Vec3) + 256];
        aleph::alloc::Arena film_arena{film_buf, sizeof(film_buf)};
        aleph::render::common::Film film =
            aleph::render::common::film_alloc(film_arena, W, H);
        aleph::threads::Pool pool(2);
        path_trace(t.scene, t.cam, t.sky, film, pool, opts);
        std::vector<Vec3> out(static_cast<std::size_t>(W) * H);
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i)
                out[static_cast<std::size_t>(j) * static_cast<std::size_t>(W) +
                    static_cast<std::size_t>(i)] =
                    film.pixels[j * film.stride_pixels + i];
        return out;
    };

    RenderOpts baseline{};
    baseline.spp        = 8;
    baseline.max_depth  = 3;
    baseline.base_seed  = 99;
    baseline.tile_size  = 16;
    // adaptive_spp defaults to false; baseline is the uniform path.

    const std::vector<Vec3> base = render(baseline);

    // Same opts again: byte-identical (determinism of the uniform path).
    const std::vector<Vec3> repeat = render(baseline);

    // Feature explicitly off but the adaptive knob cranked up: must be inert and
    // produce the SAME bytes as the baseline (SPEC §4.3 "byte-identical").
    RenderOpts off_with_scale = baseline;
    off_with_scale.adaptive_spp  = false;
    off_with_scale.max_spp_scale = 4;
    const std::vector<Vec3> off_scaled = render(off_with_scale);

    REQUIRE(base.size() == repeat.size());
    REQUIRE(base.size() == off_scaled.size());

    auto bit_equal = [](const Vec3& a, const Vec3& b) noexcept {
        return std::memcmp(&a, &b, sizeof(Vec3)) == 0;
    };

    bool repeat_ok = true, off_ok = true;
    for (std::size_t k = 0; k < base.size(); ++k) {
        if (!bit_equal(base[k], repeat[k]))     repeat_ok = false;
        if (!bit_equal(base[k], off_scaled[k])) off_ok = false;
    }
    CHECK(repeat_ok);
    CHECK(off_ok);
}

TEST_CASE("adaptive_spp: render.rt and aleph.scene contain no aleph.flow reference") {
    const std::string root = repo_root_from_file();

    const std::vector<std::string> module_files = {
        // aleph.render.rt
        "/render/src/aleph.render.rt/aleph.render.rt.cppm",
        "/render/src/aleph.render.rt/aleph.render.rt-integrator.cppm",
        "/render/src/aleph.render.rt/aleph.render.rt-material.cppm",
        "/render/src/aleph.render.rt/aleph.render.rt-path_trace.cppm",
        "/render/src/aleph.render.rt/aleph.render.rt-sampling.cppm",
        // aleph.scene
        "/render/src/aleph.scene/aleph.scene.cppm",
        "/render/src/aleph.scene/aleph.scene-scene.cppm",
        "/render/src/aleph.scene/aleph.scene-hit.cppm",
        "/render/src/aleph.scene/aleph.scene-handle32.cppm",
        "/render/src/aleph.scene/aleph.scene-sphere_soa.cppm",
        "/render/src/aleph.scene/aleph.scene-quad_soa.cppm",
        "/render/src/aleph.scene/aleph.scene-tri_soa.cppm",
        "/render/src/aleph.scene/aleph.scene-material_soa.cppm",
        "/render/src/aleph.scene/aleph.scene-bvh.cppm",
    };

    for (const auto& rel : module_files) {
        const std::string src = read_file(root + rel);
        INFO("scanning " << rel);
        // Neither the module-import spelling nor the namespace spelling may
        // appear: the render/scene layers must never depend on flow (SPEC §1/§2).
        CHECK(src.find("aleph.flow") == std::string::npos);
        CHECK(src.find("aleph::flow") == std::string::npos);
    }
}
