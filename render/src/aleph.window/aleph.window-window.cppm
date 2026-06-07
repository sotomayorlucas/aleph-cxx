module;
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <SDL2/SDL.h>

export module aleph.window:window;

import aleph.math;
import :event;

export namespace aleph::window {

class Window {
public:
    Window(int w, int h, const char* title) noexcept {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "aleph.window: SDL_Init failed: %s\n", SDL_GetError());
            std::abort();
        }
        win_ = SDL_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
        if (!win_) {
            std::fprintf(stderr, "aleph.window: SDL_CreateWindow: %s\n", SDL_GetError());
            std::abort();
        }
        back_ = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!back_) {
            std::fprintf(stderr, "aleph.window: SDL_CreateRGBSurfaceWithFormat: %s\n",
                          SDL_GetError());
            std::abort();
        }
        w_ = w; h_ = h;
    }

    ~Window() {
        if (back_) SDL_FreeSurface(back_);
        if (win_)  SDL_DestroyWindow(win_);
        SDL_Quit();
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    int poll_events(std::span<Event> out) noexcept {
        static_assert(key::Right == SDLK_RIGHT && key::Left == SDLK_LEFT
                   && key::Down  == SDLK_DOWN  && key::Up   == SDLK_UP,
                      "aleph::window::key constants must match SDL keycodes");
        int n = 0;
        SDL_Event ev;
        const SDL_Keymod mod = SDL_GetModState();
        const bool sh = (mod & KMOD_SHIFT) != 0;
        const bool ct = (mod & KMOD_CTRL)  != 0;
        const bool al = (mod & KMOD_ALT)   != 0;
        while (n < static_cast<int>(out.size()) && SDL_PollEvent(&ev)) {
            Event& e = out[static_cast<std::span<Event>::size_type>(n)];
            e = Event{};
            e.shift = sh; e.ctrl = ct; e.alt = al;
            switch (ev.type) {
                case SDL_QUIT:           e.kind = Event::Kind::Quit; ++n; break;
                case SDL_KEYDOWN:
                    e.kind = Event::Kind::KeyDown;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_KEYUP:
                    e.kind = Event::Kind::KeyUp;
                    e.key  = static_cast<int>(ev.key.keysym.sym);
                    ++n; break;
                case SDL_MOUSEBUTTONDOWN:
                    e.kind = Event::Kind::MouseDown;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEBUTTONUP:
                    e.kind = Event::Kind::MouseUp;
                    e.button = ev.button.button;
                    e.x = ev.button.x; e.y = ev.button.y;
                    ++n; break;
                case SDL_MOUSEMOTION:
                    e.kind = Event::Kind::MouseMove;
                    e.x  = ev.motion.x;     e.y  = ev.motion.y;
                    e.dx = ev.motion.xrel;  e.dy = ev.motion.yrel;
                    ++n; break;
                case SDL_MOUSEWHEEL:
                    e.kind = Event::Kind::MouseWheel;
                    e.wheel = ev.wheel.y;
                    ++n; break;
                default: break;
            }
        }
        return n;
    }

    void present() noexcept {
        SDL_Surface* ws = SDL_GetWindowSurface(win_);
        if (ws) {
            SDL_BlitSurface(back_, nullptr, ws, nullptr);
            SDL_UpdateWindowSurface(win_);
        }
    }

    aleph::math::u32* pixels() noexcept {
        return static_cast<aleph::math::u32*>(back_->pixels);
    }
    int pitch_pixels() const noexcept { return back_->pitch / 4; }
    int width()  const noexcept { return w_; }
    int height() const noexcept { return h_; }

    aleph::math::u32 ticks_ms() const noexcept { return SDL_GetTicks(); }
    aleph::math::u64 perf_counter()   const noexcept { return SDL_GetPerformanceCounter(); }
    aleph::math::u64 perf_frequency() const noexcept { return SDL_GetPerformanceFrequency(); }

private:
    SDL_Window*  win_{nullptr};
    SDL_Surface* back_{nullptr};
    int w_{0}, h_{0};
};

}  // namespace aleph::window
