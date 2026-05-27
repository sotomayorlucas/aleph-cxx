module;
#include <cstdint>
#include <string_view>
#include <algorithm>

export module aleph.editor:ui;

import aleph.math;
import aleph.render.common;
import :font;

export namespace aleph::editor {

inline aleph::math::Vec3 ui_color(aleph::math::u8 r, aleph::math::u8 g, aleph::math::u8 b) noexcept {
    return aleph::math::Vec3{
        static_cast<aleph::math::f32>(r) / 255.0f,
        static_cast<aleph::math::f32>(g) / 255.0f,
        static_cast<aleph::math::f32>(b) / 255.0f,
    };
}

struct UiCtx {
    int  mouse_x{0}, mouse_y{0};
    bool mouse_down{false};
    bool mouse_pressed{false};
    int  hot_id{0};
    int  active_id{0};
    aleph::render::common::Film* fb{nullptr};
};

inline bool in_rect(const UiCtx& u, int x, int y, int w, int h) noexcept {
    return u.mouse_x >= x && u.mouse_x < x + w && u.mouse_y >= y && u.mouse_y < y + h;
}

inline void ui_begin(UiCtx& u, aleph::render::common::Film* fb, int mx, int my,
                      bool md, bool mp) noexcept {
    u.mouse_x = mx; u.mouse_y = my;
    u.mouse_down = md; u.mouse_pressed = mp;
    u.hot_id = 0; u.fb = fb;
}

inline void ui_end(UiCtx& u) noexcept {
    if (!u.mouse_down) u.active_id = 0;
}

inline void ui_panel(UiCtx& u, int x, int y, int w, int h, std::string_view title) {
    draw_rect(*u.fb, x, y, w, h, ui_color(28, 32, 44));
    draw_rect(*u.fb, x, y, w, 20, ui_color(50, 60, 86));
    if (!title.empty()) draw_text_shadowed(*u.fb, x + 6, y + 6, title, ui_color(230, 230, 230));
    draw_rect(*u.fb, x, y, w, 1, ui_color(80, 90, 120));
    draw_rect(*u.fb, x, y + h - 1, w, 1, ui_color(15, 18, 25));
}

inline void ui_label(UiCtx& u, int x, int y, std::string_view text, aleph::math::Vec3 color) {
    draw_text_shadowed(*u.fb, x, y, text, color);
}

inline bool ui_button(UiCtx& u, int x, int y, int w, int h, std::string_view label) {
    const int id = x * 4096 + y;
    const bool over = in_rect(u, x, y, w, h);
    if (over) u.hot_id = id;
    bool clicked = false;
    if (u.hot_id == id && u.mouse_pressed) u.active_id = id;
    if (u.active_id == id && !u.mouse_down) {
        if (over) clicked = true;
        u.active_id = 0;
    }
    const aleph::math::Vec3 bg = (u.active_id == id) ? ui_color(45, 65, 110)
                                  : (u.hot_id == id)  ? ui_color(70, 90, 140)
                                                       : ui_color(50, 58, 80);
    draw_rect(*u.fb, x, y, w, h, bg);
    draw_rect(*u.fb, x, y, w, 1, ui_color(120, 140, 180));
    draw_rect(*u.fb, x, y + h - 1, w, 1, ui_color(20, 25, 35));
    if (!label.empty()) draw_text_shadowed(*u.fb, x + 6, y + (h - 8) / 2, label,
                                              aleph::math::Vec3{1, 1, 1});
    return clicked;
}

inline bool ui_slider_f(UiCtx& u, int x, int y, int w, int h,
                         aleph::math::f32& value, aleph::math::f32 minv, aleph::math::f32 maxv) {
    const int id = x * 4096 + y;
    const bool over = in_rect(u, x, y, w, h);
    if (over) u.hot_id = id;
    if (u.hot_id == id && u.mouse_pressed) u.active_id = id;
    bool changed = false;
    if (u.active_id == id) {
        const aleph::math::f32 t  = static_cast<aleph::math::f32>(u.mouse_x - x)
                                    / static_cast<aleph::math::f32>(w - 1);
        const aleph::math::f32 tc = std::clamp(t, 0.0f, 1.0f);
        const aleph::math::f32 nv = minv + tc * (maxv - minv);
        if (nv != value) { value = nv; changed = true; }
    }
    draw_rect(*u.fb, x, y, w, h, ui_color(20, 25, 35));
    const aleph::math::f32 t = std::clamp((value - minv) / (maxv - minv), 0.0f, 1.0f);
    const int fw = static_cast<int>(t * static_cast<aleph::math::f32>(w));
    const aleph::math::Vec3 fg = (u.active_id == id) ? ui_color(110, 160, 230)
                                  : (u.hot_id == id)  ? ui_color(90, 140, 210)
                                                       : ui_color(75, 120, 190);
    if (fw > 0) draw_rect(*u.fb, x, y, fw, h, fg);
    const int handle_x = x + fw - 2;
    if (handle_x >= x && handle_x + 4 <= x + w)
        draw_rect(*u.fb, handle_x, y - 1, 4, h + 2, ui_color(230, 240, 255));
    return changed;
}

}  // namespace aleph::editor
