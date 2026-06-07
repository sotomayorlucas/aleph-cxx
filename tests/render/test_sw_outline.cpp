#include "doctest.h"

#include <vector>

import aleph.math;          // Vec3
import aleph.render.common; // Film
import aleph.render.sw;     // draw_selection_outline

using aleph::math::Vec3;
using aleph::math::f32;

TEST_CASE("draw_selection_outline rings the covered silhouette, not the inside") {
    constexpr int W = 9, H = 9;
    std::vector<Vec3> px(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), Vec3{0.0f, 0.0f, 0.0f});
    aleph::render::common::Film fb{px.data(), W, H, W};

    // Coverage: a 3x3 block at rows/cols [3..5] (depth > 0 == covered).
    std::vector<f32> sel_depth(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0.0f);
    for (int y = 3; y <= 5; ++y)
        for (int x = 3; x <= 5; ++x)
            sel_depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)] = 1.0f;

    const Vec3 orange{1.0f, 0.5f, 0.1f};
    aleph::render::sw::draw_selection_outline(fb, sel_depth, /*radius=*/1, orange);

    auto at = [&](int x, int y) -> Vec3 { return px[static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)]; };

    // A covered (inside) pixel is NOT painted (outline lives OUTSIDE the silhouette).
    CHECK(at(4, 4).x == doctest::Approx(0.0f));
    // A pixel just outside the block, adjacent to coverage, IS painted.
    CHECK(at(2, 4).x == doctest::Approx(1.0f));   // left of the block
    CHECK(at(4, 2).y == doctest::Approx(0.5f));   // above the block
    CHECK(at(6, 6).z == doctest::Approx(0.1f));   // diagonal corner (Chebyshev r=1)
    // A far pixel (distance > radius from any covered pixel) is untouched.
    CHECK(at(0, 0).x == doctest::Approx(0.0f));
}
