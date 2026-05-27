import aleph.window;

int main() {
    aleph::window::Event e{};
    return static_cast<int>(e.kind) == 0 ? 0 : 0;
}
