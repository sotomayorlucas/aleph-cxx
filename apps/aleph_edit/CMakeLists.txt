# Executable target name disambiguated from the `aleph_edit` module library
# (bridge/src/aleph.edit); the produced binary is still named `aleph_edit`.
add_executable(aleph_edit_app main.cpp)
set_target_properties(aleph_edit_app PROPERTIES OUTPUT_NAME aleph_edit)
target_link_libraries(aleph_edit_app PRIVATE
    aleph_edit aleph_window aleph_editor
    aleph_render_sw aleph_render_rt aleph_render_common aleph_lowering
    aleph_graph aleph_types aleph_scene aleph_io aleph_dpo
    aleph_math aleph_threads aleph_alloc
    aleph_flags_test)
# The SDL shell (live hybrid loop + UI) is gated on this; the target is only
# added when ALEPH_HAVE_SDL2 is ON (apps/CMakeLists.txt), so define it here so
# the `#if defined(ALEPH_HAVE_SDL2)` shell paths compile in. `--headless`
# (the testable artifact) needs no window and is always available.
target_compile_definitions(aleph_edit_app PRIVATE ALEPH_HAVE_SDL2=1)
