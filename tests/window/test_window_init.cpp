#include "doctest.h"
#include <cstdlib>
import aleph.window;

using namespace aleph::window;

TEST_CASE("Window creates + destroys without crash") {
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        WARN("No display — skipping window init test.");
        return;
    }
    Window w(160, 120, "aleph_test_window");
    CHECK(w.width()  == 160);
    CHECK(w.height() == 120);
    CHECK(w.pixels() != nullptr);
}
