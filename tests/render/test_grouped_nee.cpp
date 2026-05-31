// SPEC §5 test 5 — grouped_nee_unbiased (render).
//
// A scene whose lights form a SINGLE group must render the same mean luminance
// whether the integrator uses the all-lights-sum NEE path (grouped_nee=false,
// the default) or the opt-in group-stratified NEE path (grouped_nee=true). With
// one group the stratified estimator picks one light per group weighted by the
// group size (= the lone light, weight 1), so it is unbiased and must match the
// all-sum path in expectation. This is the unbiasedness sanity check of SPEC
// §4.3 / §7 ("grouped path unbiased on a single-group scene").
//
// Architectural note (SPEC §4.3 / §2): the renderer never imports aleph.sheaf.
// It reads the grouping ONLY from scene.light_groups, a plain table on Scene.
// This test sets that table by hand (a single group holding every light handle)
// rather than going through the lowering, keeping the render layer isolated.

#include "doctest.h"

#include <cmath>

import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::rt;
using aleph::math::Vec3;
using aleph::scene::Scene;
using aleph::scene::Handle32;

namespace {

// Rec.709 relative luminance of a linear-RGB radiance value.
[[nodiscard]] float luminance(const Vec3& c) noexcept {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// Render a small, genuinely-lit box scene and return its mean pixel luminance.
// A grey diffuse floor and back wall catch primary rays on NEE-using surfaces;
// a single emissive ceiling quad is the only light, so the SINGLE group it
// belongs to is genuine. `grouped_nee` selects the integrator path under test.
[[nodiscard]] float mean_luminance(bool grouped_nee) {
    Scene s;

    const auto m_floor = aleph::scene::scene_add_lambertian(s, Vec3{0.73f, 0.73f, 0.73f});
    const auto m_wall  = aleph::scene::scene_add_lambertian(s, Vec3{0.65f, 0.55f, 0.45f});
    const auto m_light = aleph::scene::scene_add_emissive(s, Vec3{14.0f, 14.0f, 14.0f});

    // Floor quad in the y=0 plane, spanning x,z in [0,5].
    aleph::scene::scene_add_quad(s, Vec3{0, 0, 0}, Vec3{5, 0, 0}, Vec3{0, 0, 5}, m_floor);
    // Back wall in the z=5 plane, spanning x in [0,5], y in [0,5].
    aleph::scene::scene_add_quad(s, Vec3{0, 0, 5}, Vec3{5, 0, 0}, Vec3{0, 5, 0}, m_wall);
    // Emissive ceiling light, facing down (normal = u x v points -y), centered
    // above the floor. This is the ONLY light, so the single group is genuine.
    const Handle32 light = aleph::scene::scene_add_quad(
        s, Vec3{1.5f, 4.0f, 1.5f}, Vec3{2, 0, 0}, Vec3{0, 0, 2}, m_light);

    REQUIRE(s.lights.size() == 1);
    REQUIRE(s.lights[0].packed == light.packed);

    // SPEC §4.2: a single group holding every light handle. The grouped path
    // reads exactly this; the all-sum path ignores it.
    s.light_groups.clear();
    s.light_groups.push_back({light});

    alignas(16) static unsigned char scratch[64 * 1024];
    aleph::alloc::Arena arena{scratch, sizeof(scratch)};
    aleph::scene::scene_build_bvh(s, arena);

    constexpr int W = 24;
    constexpr int H = 24;
    alignas(64) static unsigned char film_buf[W * H * sizeof(Vec3) + 256];
    aleph::alloc::Arena film_arena{film_buf, sizeof(film_buf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);

    // Camera looking at the floor/light from the front.
    const aleph::render::common::Camera cam =
        aleph::render::common::make_camera(Vec3{2.5f, 2.0f, -6.0f}, Vec3{2.5f, 1.5f, 2.5f},
                                            Vec3{0, 1, 0}, 50.0f, W, H, 0.0f, 10.0f);
    // Dark sky so the lit surfaces dominate the luminance signal.
    aleph::render::common::Sky sky{Vec3{0.02f, 0.02f, 0.02f}, Vec3{0.04f, 0.05f, 0.07f}};

    aleph::threads::Pool pool(2);
    RenderOpts opts{};
    opts.spp         = 24;
    opts.max_depth   = 6;
    opts.base_seed   = 1234;
    opts.tile_size   = 16;
    opts.grouped_nee = grouped_nee;
    path_trace(s, cam, sky, film, pool, opts);

    double sum = 0.0;
    const int n = film.width * film.height;
    for (int j = 0; j < film.height; ++j)
        for (int i = 0; i < film.width; ++i)
            sum += static_cast<double>(luminance(film.pixels[j * film.stride_pixels + i]));
    return static_cast<float>(sum / static_cast<double>(n));
}

}  // namespace

TEST_CASE("grouped_nee single-group: mean luminance matches all-sum (unbiased)") {
    const float mean_all     = mean_luminance(/*grouped_nee=*/false);
    const float mean_grouped = mean_luminance(/*grouped_nee=*/true);

    // The scene is genuinely lit: a degenerate (all-black) render would make the
    // tolerance check vacuous, so require a real signal first.
    CHECK(mean_all > 1e-3f);
    CHECK(mean_grouped > 1e-3f);

    // Unbiasedness sanity: with a single light group the stratified estimator is
    // the same estimator as the all-sum path (one light, weight 1), so the means
    // must agree well within Monte-Carlo tolerance. Use a relative tolerance with
    // an absolute floor to stay robust to low-spp noise without being vacuous.
    const float diff = std::fabs(mean_all - mean_grouped);
    const float tol  = 0.05f * mean_all + 1e-3f;  // 5% relative + small absolute floor
    INFO("mean_all=" << mean_all << " mean_grouped=" << mean_grouped
                     << " diff=" << diff << " tol=" << tol);
    CHECK(diff <= tol);
}
