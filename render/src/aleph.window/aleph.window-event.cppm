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

}  // namespace aleph::window
