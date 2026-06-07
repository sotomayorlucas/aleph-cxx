export module aleph.window:event;

export namespace aleph::window {

struct Event {
    enum class Kind { Quit, KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseWheel };
    Kind kind;
    int  key{0};
    int  button{0};
    int  x{0}, y{0};
    int  dx{0}, dy{0};
    int  wheel{0};
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
};

// Named non-ASCII keys. Values are the SDL keycodes for the arrow keys
// (SDLK_RIGHT/LEFT/DOWN/UP = 0x40000000 | scancode). aleph.window-window.cppm
// static_asserts these equal SDLK_* so they cannot drift; keeping them here lets
// the app compare Event.key without including SDL.
namespace key {
    inline constexpr int Right = 1073741903;
    inline constexpr int Left  = 1073741904;
    inline constexpr int Down  = 1073741905;
    inline constexpr int Up    = 1073741906;
}

}  // namespace aleph::window
