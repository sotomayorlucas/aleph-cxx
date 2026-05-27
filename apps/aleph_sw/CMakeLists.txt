add_executable(aleph_sw main.cpp)
target_link_libraries(aleph_sw PRIVATE
    aleph_scene aleph_render_sw aleph_render_common
    aleph_window aleph_editor
    aleph_math aleph_alloc aleph_threads
    aleph_flags_test)
