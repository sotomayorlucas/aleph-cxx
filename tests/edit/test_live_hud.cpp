#include "doctest.h"

#include <string>

#include "../../apps/aleph_edit/live_hud.hpp"

TEST_CASE("live HUD labels prioritize wave, import, project, then scene") {
    aleph::app::edit::LiveHudSnapshot state{};
    CHECK(aleph::app::edit::live_hud_mode_label(state) == "SCENE");

    state.loaded_project = true;
    CHECK(aleph::app::edit::live_hud_mode_label(state) == "PROJECT");

    state.imported_obj = true;
    CHECK(aleph::app::edit::live_hud_mode_label(state) == "OBJ");

    state.wave_demo = true;
    CHECK(aleph::app::edit::live_hud_mode_label(state) == "WAVE");
}

TEST_CASE("live HUD save label exposes actionable project state") {
    aleph::app::edit::LiveHudSnapshot state{};
    CHECK(aleph::app::edit::live_hud_save_label(state) == "NO SAVE");

    state.has_save_path = true;
    CHECK(aleph::app::edit::live_hud_save_label(state) == "SAVED");

    state.dirty = true;
    CHECK(aleph::app::edit::live_hud_save_label(state) == "DIRTY");

    state.save_failed = true;
    CHECK(aleph::app::edit::live_hud_save_label(state) == "SAVE ERROR");
}

TEST_CASE("live HUD top line is compact but includes interaction status") {
    aleph::app::edit::LiveHudSnapshot state{};
    state.imported_obj = true;
    state.entities = 12;
    state.lights = 2;
    state.faces = 345;
    state.can_undo = true;
    state.has_save_path = true;
    state.dirty = true;

    const std::string line = aleph::app::edit::live_hud_top_line(state);
    CHECK(line == "OBJ  ENT 12  LGT 2  FACE 345  U:Y  R:N  DIRTY");
}
