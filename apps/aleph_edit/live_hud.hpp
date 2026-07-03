#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace aleph::app::edit {

struct LiveHudSnapshot {
    std::size_t entities{0};
    std::size_t lights{0};
    std::size_t faces{0};

    bool wave_demo{false};
    bool loaded_project{false};
    bool imported_obj{false};

    bool can_undo{false};
    bool can_redo{false};
    bool has_save_path{false};
    bool dirty{false};
    bool save_failed{false};
};

inline constexpr std::string_view live_hud_mode_label(
    const LiveHudSnapshot& state) noexcept {
    if (state.wave_demo) return "WAVE";
    if (state.imported_obj) return "OBJ";
    if (state.loaded_project) return "PROJECT";
    return "SCENE";
}

inline constexpr std::string_view live_hud_save_label(
    const LiveHudSnapshot& state) noexcept {
    if (state.save_failed) return "SAVE ERROR";
    if (!state.has_save_path) return "NO SAVE";
    if (state.dirty) return "DIRTY";
    return "SAVED";
}

inline std::string live_hud_history_line(const LiveHudSnapshot& state) {
    char line[48];
    std::snprintf(line, sizeof(line), "UNDO %s  REDO %s",
                  state.can_undo ? "READY" : "EMPTY",
                  state.can_redo ? "READY" : "EMPTY");
    return std::string{line};
}

inline std::string live_hud_top_line(const LiveHudSnapshot& state) {
    char line[160];
    std::snprintf(line, sizeof(line),
                  "%.*s  ENT %zu  LGT %zu  FACE %zu  U:%c  R:%c  %.*s",
                  static_cast<int>(live_hud_mode_label(state).size()),
                  live_hud_mode_label(state).data(),
                  state.entities,
                  state.lights,
                  state.faces,
                  state.can_undo ? 'Y' : 'N',
                  state.can_redo ? 'Y' : 'N',
                  static_cast<int>(live_hud_save_label(state).size()),
                  live_hud_save_label(state).data());
    return std::string{line};
}

}  // namespace aleph::app::edit
