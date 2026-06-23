#include <cmath>
#include "doctest.h"
#include <vector>
#include <algorithm>
#include <cstddef>
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
import aleph.alloc;
import aleph.threads;

using namespace aleph::render::sw;

TEST_CASE("rast_scan_textured: writes some pixels for a centered triangle") {
    alignas(64) static unsigned char fbuf[16 * 16 * 16];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film color = aleph::render::common::film_alloc(arena, 16, 16);
    for (int i = 0; i < 16 * 16; ++i) color.pixels[i] = aleph::math::Vec3{1, 0, 1};
    std::vector<aleph::math::f32> depth(16 * 16, 0.0f);
    SpanBuffer sb(16, 16);

    ScreenVert v0{2.0f,  2.0f, 0.0f, 0, 0, 1.0f};
    ScreenVert v1{14.0f, 2.0f, 0.0f, 1, 0, 1.0f};
    ScreenVert v2{8.0f, 14.0f, 0.0f, 0, 1, 1.0f};

    rast_scan_textured(color, depth, sb, v0, v1, v2,
                        tex_floor, nullptr, /*two_sided=*/false, 0, 16);

    int touched = 0;
    for (int i = 0; i < 16 * 16; ++i)
        if (!(color.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    CHECK(touched > 0);
}

namespace {

// A position-independent white texel. The back-face test mirrors the image
// top/bottom (eye above vs below), but tex_floor samples a v-asymmetric pattern
// (tex(u,v) != tex(u,1-v)), so it would break a byte-exact mirror even with
// perfect geometry. A flat texel isolates the per-vertex Gouraud interpolation
// — the actual thing the v1/v2 swap must carry — from texture sampling.
aleph::math::u32 tex_flat(aleph::math::f32, aleph::math::f32) noexcept {
    return 0xFFFFFFFFu;
}

// A single floor quad at y=0, wound CW seen from ABOVE (the engine's front
// side), with no lightmap. `two_sided` is the flag under test. When
// `gradient` is set, the four corners carry a non-trivial per-vertex Gouraud
// tint so the interior is an interpolated colour (not a flat fill) — this is
// what the back-face test asserts is carried correctly through the v1/v2 swap —
// and the texture is switched to the flat texel so only the vcol gradient
// drives the interior colour.
SceneRT make_floor_face(bool two_sided, bool gradient = false) {
    SceneRT sr;
    Face f{};
    f.verts = {aleph::math::Vec3{-2, 0, -2}, aleph::math::Vec3{2, 0, -2},
               aleph::math::Vec3{2, 0, 2},   aleph::math::Vec3{-2, 0, 2}};
    f.uvs   = {aleph::math::Vec2{0, 0}, aleph::math::Vec2{1, 0},
               aleph::math::Vec2{1, 1}, aleph::math::Vec2{0, 1}};
    f.tex = gradient ? tex_flat : tex_floor;
    f.lightmap_id = 0xFFFFFFFFu;
    f.two_sided = two_sided;
    if (gradient)
        f.vcol = {aleph::math::Vec3{0.20f, 0.30f, 0.40f},
                  aleph::math::Vec3{0.90f, 0.30f, 0.40f},
                  aleph::math::Vec3{0.90f, 0.80f, 0.40f},
                  aleph::math::Vec3{0.20f, 0.80f, 0.40f}};
    sr.faces.push_back(f);
    return sr;
}

constexpr int kW = 32, kH = 32;

// Rasterize `sr` from `eye` looking at the origin into caller-owned buffers.
void render_scene(const SceneRT& sr, aleph::math::Vec3 eye,
                  aleph::render::common::Film& film,
                  std::vector<aleph::math::f32>& depth) {
    for (int i = 0; i < kW * kH; ++i) film.pixels[i] = aleph::math::Vec3{1, 0, 1};
    depth.assign(kW * kH, 0.0f);  // 1/w far = 0 (nearer = larger)
    const aleph::math::Mat4 view = aleph::math::Mat4::look_at(
        eye, aleph::math::Vec3{0, 0, 0}, aleph::math::Vec3{0, 1, 0});
    const aleph::math::Mat4 proj = aleph::math::Mat4::perspective(
        aleph::math::deg_to_rad(60.0f), 1.0f, 0.05f, 100.0f);
    aleph::threads::Pool pool(2);
    rasterize(sr, proj * view, film, depth, pool);
}

int count_touched(const aleph::render::common::Film& film) {
    int touched = 0;
    for (int i = 0; i < kW * kH; ++i)
        if (!(film.pixels[i] == aleph::math::Vec3{1, 0, 1})) ++touched;
    return touched;
}

}  // namespace

TEST_CASE("rasterize: two_sided quad seen from the BACK covers pixels + writes depth") {
    alignas(64) static unsigned char fbuf[kW * kH * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(arena, kW, kH);
    std::vector<aleph::math::f32> depth;

    const SceneRT sr = make_floor_face(/*two_sided=*/true);
    render_scene(sr, aleph::math::Vec3{0, -2, 5}, film, depth);  // eye BELOW the floor

    CHECK(count_touched(film) > 0);
    aleph::math::f32 max_depth = 0.0f;
    for (const aleph::math::f32 d : depth) max_depth = std::max(max_depth, d);
    CHECK(max_depth > 0.0f);  // covered pixels must also land in the z-buffer
}

TEST_CASE("rasterize: two_sided BACK face interpolates attributes like the FRONT") {
    // Coverage + depth (above) don't prove the v1/v2 re-wind carried the
    // per-vertex uv/colour through to interpolation — a swap that dropped them
    // would still cover pixels. So render the SAME gradient quad (flat texel, so
    // only the vcol gradient drives the interior) from the front and the back.
    // The back eye is the front eye reflected across the quad's y=0 plane with
    // the same up vector, so the back image is the front image mirrored
    // vertically: back(x, y) == front(x, kH-1-y). At an interior pixel the
    // interpolated colour must agree — to a tolerance, NOT bit-exactly: the
    // perspective gradients sum the same floats in a different order after the
    // swap, so front/back differ by ~1 ulp (~1e-7). A dropped or mis-wound
    // attribute would diverge by O(0.1+), far above the tolerance.
    alignas(64) static unsigned char fbuf_f[kW * kH * sizeof(aleph::math::Vec3)];
    alignas(64) static unsigned char fbuf_b[kW * kH * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena_f{fbuf_f, sizeof(fbuf_f)};
    aleph::alloc::Arena arena_b{fbuf_b, sizeof(fbuf_b)};
    aleph::render::common::Film film_f = aleph::render::common::film_alloc(arena_f, kW, kH);
    aleph::render::common::Film film_b = aleph::render::common::film_alloc(arena_b, kW, kH);
    std::vector<aleph::math::f32> depth_f, depth_b;

    render_scene(make_floor_face(/*two_sided=*/true, /*gradient=*/true),
                 aleph::math::Vec3{0, 2, 5}, film_f, depth_f);   // eye ABOVE (front)
    render_scene(make_floor_face(/*two_sided=*/true, /*gradient=*/true),
                 aleph::math::Vec3{0, -2, 5}, film_b, depth_b);  // eye BELOW (back)

    REQUIRE(film_f.pixels != nullptr);
    REQUIRE(film_b.pixels != nullptr);

    // px=16,py=15 is solidly in the quad interior (two rows clear of the top
    // coverage edge, so the fill-rule asymmetry under the mirror can't reach it).
    constexpr int px = 16, py = 15;
    const aleph::math::Vec3 front = film_f.pixels[py * kW + px];
    const aleph::math::Vec3 back  = film_b.pixels[(kH - 1 - py) * kW + px];
    REQUIRE(!(front == aleph::math::Vec3{1, 0, 1}));  // interior, not background
    // A genuine interpolation: not flat-equal to any single corner colour.
    REQUIRE(!(front == aleph::math::Vec3{0.20f, 0.30f, 0.40f}));
    constexpr aleph::math::f32 kEps = 1e-4f;
    CHECK(std::fabs(back.x - front.x) < kEps);  // back-face interpolation tracks
    CHECK(std::fabs(back.y - front.y) < kEps);  // the front face (per channel,
    CHECK(std::fabs(back.z - front.z) < kEps);  // to a 1-ulp-scale tolerance)
}

TEST_CASE("rasterize: one-sided quad seen from the BACK is culled (zero coverage)") {
    alignas(64) static unsigned char fbuf[kW * kH * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena{fbuf, sizeof(fbuf)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(arena, kW, kH);
    std::vector<aleph::math::f32> depth;

    const SceneRT sr = make_floor_face(/*two_sided=*/false);
    render_scene(sr, aleph::math::Vec3{0, -2, 5}, film, depth);  // eye BELOW the floor

    CHECK(count_touched(film) == 0);  // today's behaviour, preserved when flagged off
    for (const aleph::math::f32 d : depth) CHECK(d == 0.0f);
}

TEST_CASE("rasterize: FRONT view is byte-identical with two_sided on vs off") {
    // Guard against accidental gradient/edge-setup changes: from the front the
    // signed area is positive, so the two_sided path must be a strict no-op.
    alignas(64) static unsigned char fbuf_a[kW * kH * sizeof(aleph::math::Vec3)];
    alignas(64) static unsigned char fbuf_b[kW * kH * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena arena_a{fbuf_a, sizeof(fbuf_a)};
    aleph::alloc::Arena arena_b{fbuf_b, sizeof(fbuf_b)};
    aleph::render::common::Film film_a = aleph::render::common::film_alloc(arena_a, kW, kH);
    aleph::render::common::Film film_b = aleph::render::common::film_alloc(arena_b, kW, kH);
    std::vector<aleph::math::f32> depth_a, depth_b;

    const aleph::math::Vec3 eye{0, 2, 5};  // eye ABOVE the floor (front side)
    render_scene(make_floor_face(true),  eye, film_a, depth_a);
    render_scene(make_floor_face(false), eye, film_b, depth_b);

    CHECK(count_touched(film_a) > 0);  // the front view actually covers pixels
    bool identical = true;
    for (int i = 0; i < kW * kH; ++i) {
        if (!(film_a.pixels[i] == film_b.pixels[i])) identical = false;
        if (depth_a[static_cast<std::size_t>(i)] != depth_b[static_cast<std::size_t>(i)])
            identical = false;
    }
    CHECK(identical);
}
