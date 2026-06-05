#include "doctest.h"
import aleph.render.sw;

using namespace aleph::render::sw;

// SpanBuffer is a thin per-scanline span clamp; it no longer does coverage-based
// hidden-surface removal (that moved to the per-pixel 1/w z-buffer in rast_scan,
// because face-centre-sorted coverage buried large triangles' neighbours — e.g. a
// sphere resting on the floor quad). So every emit draws its full clamped span.

TEST_CASE("SpanBuffer: emit one full span → drawn count == width") {
    SpanBuffer sb(16, 1);
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 16);
    CHECK(sb.pixels_drawn() == 16);
}

TEST_CASE("SpanBuffer: overlapping emits both draw (no coverage HSR)") {
    SpanBuffer sb(16, 1);
    sb.emit(0, 0, 16, [](int, int, int){});
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 16);             // second emit is NOT skipped
    CHECK(sb.pixels_drawn() == 32);
}

TEST_CASE("SpanBuffer: spans are clamped to [0,w) and dropped off-screen") {
    SpanBuffer sb(16, 4);
    int drawn = 0;
    sb.emit(1, -5, 100, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 16);            // clamped to the 16-px width
    sb.emit(99, 0, 16, [&](int, int, int) { CHECK(false); });  // y out of range → no call
}
