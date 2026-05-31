add_executable(aleph_lower_demo main.cpp)
target_link_libraries(aleph_lower_demo PRIVATE
    aleph_lowering aleph_graph aleph_types
    aleph_scene aleph_render_rt aleph_render_common
    aleph_math aleph_alloc aleph_threads
    aleph_flags_test)
