# Executable target name disambiguated from the `aleph_edit` module library
# (bridge/src/aleph.edit); the produced binary is still named `aleph_edit`.
add_executable(aleph_edit_app main.cpp)
set_target_properties(aleph_edit_app PROPERTIES OUTPUT_NAME aleph_edit)
target_link_libraries(aleph_edit_app PRIVATE
    aleph_edit aleph_window
    aleph_render_sw aleph_render_rt aleph_render_common aleph_lowering
    aleph_graph aleph_types aleph_scene
    aleph_math aleph_threads aleph_alloc
    aleph_flags_test)
