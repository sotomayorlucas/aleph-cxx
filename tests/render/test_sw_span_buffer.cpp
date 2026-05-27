#include "doctest.h"
import aleph.render.sw;

using namespace aleph::render::sw;

TEST_CASE("SpanBuffer: empty → emit one full span → drawn count == width") {
    SpanBuffer sb(16, 1);
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 16);
    CHECK(sb.pixels_drawn() == 16);
}

TEST_CASE("SpanBuffer: second emit on same row → drawn 0 (already covered)") {
    SpanBuffer sb(16, 1);
    sb.emit(0, 0, 16, [](int, int, int){});
    int drawn = 0;
    sb.emit(0, 0, 16, [&](int, int x0, int x1) { drawn += x1 - x0; });
    CHECK(drawn == 0);
    CHECK(sb.pixels_skipped() >= 16);
}
