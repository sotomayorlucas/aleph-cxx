add_executable(aleph_bench
    bench_main.cpp)
target_include_directories(aleph_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aleph_bench PRIVATE
    aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads
    aleph_flags_test aleph_scene aleph_render_rt aleph_render_common)

add_executable(aleph_bench_scaling
    bench_scaling.cpp)
target_link_libraries(aleph_bench_scaling PRIVATE
    aleph_flags_test aleph_math aleph_containers
    aleph_types aleph_graph aleph_sheaf aleph_flow
    aleph_linalg_gf2 aleph_linalg_sparse)
