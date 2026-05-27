add_executable(aleph_rt main.cpp)
target_link_libraries(aleph_rt PRIVATE
    aleph_scene aleph_render_rt aleph_render_common
    aleph_math aleph_alloc aleph_threads aleph_io
    aleph_flags_test)
