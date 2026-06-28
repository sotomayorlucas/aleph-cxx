#include "doctest.h"
#include <SDL2/SDL.h>
#include <cstdlib>
import aleph.window;

using namespace aleph::window;

TEST_CASE("Window creates + destroys without crash") {
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        WARN("No display — skipping window init test.");
        return;
    }
    // DISPLAY may be set while X/Wayland is unreachable (e.g. broken XAUTHORITY).
    // Probe SDL before constructing Window — it aborts on SDL_Init failure.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        WARN("SDL video unavailable — skipping window init test.");
        return;
    }
    SDL_Quit();

    Window w(160, 120, "aleph_test_window");
    CHECK(w.width()  == 160);
    CHECK(w.height() == 120);
    CHECK(w.pixels() != nullptr);
}
