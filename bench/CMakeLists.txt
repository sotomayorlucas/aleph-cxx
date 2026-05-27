add_executable(aleph_bench
    bench_main.cpp)
target_include_directories(aleph_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aleph_bench PRIVATE
    aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads
    aleph_flags_test aleph_scene aleph_render_rt aleph_render_common)
