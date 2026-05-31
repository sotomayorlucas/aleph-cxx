#include <cstdio>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <span>

import aleph.math;
import aleph.render.common;
import aleph.render.sw;
import aleph.window;
import aleph.editor;
import aleph.alloc;
import aleph.threads;

int main(int /*argc*/, char** /*argv*/) {
    constexpr int W = 800, H = 600;
    aleph::window::Window win(W, H, "aleph_sw editor");

    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, aleph::math::Vec3{0, 0, 0}, 24.0f,
                                  aleph::render::sw::tex_floor);
    aleph::render::sw::add_pillar(sr, aleph::math::Vec3{0, 0, 0}, 0.8f, 3.5f,
                                   aleph::render::sw::tex_brick);
    aleph::render::sw::add_cube(sr, aleph::math::Vec3{3, 0.5f, 1.5f}, 1.0f,
                                 aleph::render::sw::tex_brick);
    aleph::render::sw::add_cube(sr, aleph::math::Vec3{-2.5f, 0.4f, -1.8f}, 0.8f,
                                 aleph::render::sw::tex_checker);

    constexpr int LM = 32;
    sr.lightmap_pool.assign(sr.faces.size() * LM * LM, 0u);
    for (std::size_t i = 0; i < sr.lightmaps.size(); ++i) {
        sr.lightmaps[i].texels = sr.lightmap_pool.data() + i * LM * LM;
        sr.lightmaps[i].w = LM; sr.lightmaps[i].h = LM;
    }
    aleph::math::Vec3 light_pos{3, 8, 4};
    aleph::math::f32 intensity = 35.0f;
    aleph::math::f32 ambient   = 0.12f;
    aleph::render::sw::bake_lightmaps(sr, light_pos, intensity, ambient);

    aleph::editor::OrbitCam cam{aleph::math::Vec3{0, 1, 0}, 0.3f, 0.25f, 8.0f};
    aleph::editor::UiCtx ui{};

    aleph::threads::Pool pool(4);

    alignas(64) static unsigned char film_scratch[W * H * sizeof(aleph::math::Vec3)];
    aleph::alloc::Arena film_arena{film_scratch, sizeof(film_scratch)};
    aleph::render::common::Film film = aleph::render::common::film_alloc(film_arena, W, H);
    std::vector<aleph::math::f32> depth(W * H, 1.0f);

    bool running = true;
    int mouse_x = 0, mouse_y = 0;
    bool left_down = false, right_down = false, prev_left = false;
    int selected_face = -1;

    while (running) {
        std::array<aleph::window::Event, 64> evbuf{};
        const int nev = win.poll_events(std::span<aleph::window::Event>{evbuf});
        bool clicked_pick = false;
        for (std::size_t i = 0; i < static_cast<std::size_t>(nev); ++i) {
            const auto& e = evbuf[i];
            switch (e.kind) {
                case aleph::window::Event::Kind::Quit: running = false; break;
                case aleph::window::Event::Kind::KeyDown:
                    if (e.key == 27 /*SDLK_ESCAPE*/) running = false;
                    break;
                case aleph::window::Event::Kind::MouseDown:
                    if (e.button == 1) { left_down = true; clicked_pick = true; }
                    if (e.button == 3) right_down = true;
                    mouse_x = e.x; mouse_y = e.y;
                    break;
                case aleph::window::Event::Kind::MouseUp:
                    if (e.button == 1) left_down = false;
                    if (e.button == 3) right_down = false;
                    break;
                case aleph::window::Event::Kind::MouseMove:
                    mouse_x = e.x; mouse_y = e.y;
                    aleph::editor::orbit_handle(cam, e, left_down, right_down);
                    break;
                case aleph::window::Event::Kind::MouseWheel:
                    aleph::editor::orbit_handle(cam, e, left_down, right_down);
                    break;
                default: break;
            }
        }

        // Clear film to sky gradient.
        for (int y = 0; y < H; ++y) {
            const aleph::math::f32 t = static_cast<aleph::math::f32>(y) /
                                        static_cast<aleph::math::f32>(H - 1);
            const aleph::math::Vec3 c =
                aleph::math::lerp(aleph::math::Vec3{0.43f, 0.55f, 0.71f},
                                    aleph::math::Vec3{0.20f, 0.31f, 0.55f}, t);
            for (int x = 0; x < W; ++x)
                film.pixels[y * film.stride_pixels + x] = c;
        }

        const aleph::math::Vec3 eye = aleph::editor::orbit_eye(cam);
        const aleph::math::Mat4 view = aleph::math::Mat4::look_at(
            eye, cam.target, aleph::math::Vec3{0, 1, 0});
        const aleph::math::Mat4 proj = aleph::math::Mat4::perspective(
            aleph::math::deg_to_rad(60.0f),
            static_cast<aleph::math::f32>(W) / static_cast<aleph::math::f32>(H),
            0.05f, 100.0f);
        const aleph::math::Mat4 mvp = proj * view;

        aleph::render::sw::rasterize(sr, mvp, film, depth, pool);

        if (clicked_pick && !prev_left) {
            selected_face = aleph::editor::pick_face(sr, mouse_x, mouse_y, eye, cam.target,
                aleph::math::Vec3{0, 1, 0},
                aleph::math::deg_to_rad(60.0f),
                static_cast<aleph::math::f32>(W) / static_cast<aleph::math::f32>(H),
                W, H);
        }

        const bool ui_mouse_pressed = left_down && !prev_left;
        prev_left = left_down;

        aleph::editor::ui_begin(ui, &film, mouse_x, mouse_y, left_down, ui_mouse_pressed);
        aleph::editor::ui_panel(ui, W - 240, 60, 230, 180, "LIGHT");
        aleph::editor::ui_label(ui, W - 232, 90, "INTENSITY",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_int = intensity;
        aleph::editor::ui_slider_f(ui, W - 232, 106, 214, 16, intensity, 0.0f, 100.0f);
        aleph::editor::ui_label(ui, W - 232, 130, "AMBIENT",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_amb = ambient;
        aleph::editor::ui_slider_f(ui, W - 232, 146, 214, 16, ambient, 0.0f, 0.5f);
        aleph::editor::ui_label(ui, W - 232, 170, "LIGHT HEIGHT",
                                  aleph::math::Vec3{1, 1, 1});
        const auto prev_ly = light_pos.y;
        aleph::editor::ui_slider_f(ui, W - 232, 186, 214, 16, light_pos.y, 1.0f, 20.0f);
        if (intensity != prev_int || ambient != prev_amb || light_pos.y != prev_ly) {
            aleph::render::sw::bake_lightmaps(sr, light_pos, intensity, ambient);
        }
        aleph::editor::ui_end(ui);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "FACES %zu  SELECTED %d",
                        sr.faces.size(), selected_face);
        aleph::editor::draw_rect(film, 8, 8, 360, 24, aleph::math::Vec3{0, 0, 0});
        aleph::editor::draw_text_shadowed(film, 14, 14, buf,
                                            aleph::math::Vec3{1, 1, 1});

        // Tonemap film (Vec3 linear) into the SDL window's ARGB pixels.
        aleph::math::u32* wpx = win.pixels();
        const int wp = win.pitch_pixels();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const aleph::math::Vec3 lin = film.pixels[y * film.stride_pixels + x];
                wpx[y * wp + x] = aleph::render::common::tonemap_argb8888_gamma2(lin);
            }
        }
        win.present();
    }
    return 0;
}
