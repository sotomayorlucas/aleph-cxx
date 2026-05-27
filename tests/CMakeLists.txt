add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp
    cpu/test_cpu.cpp
    math/test_types.cpp
    math/test_vec.cpp
    math/test_mat.cpp
    math/test_quat.cpp
    math/test_rotor.cpp
    math/test_bivector.cpp
    math/test_multivector.cpp
    math/test_rotor_advanced.cpp
    math/test_dual.cpp
    math/test_tangent.cpp
    math/test_geom.cpp
    alloc/test_arena.cpp
    alloc/test_frame.cpp
    alloc/test_slab.cpp
    alloc/test_freelist.cpp
    alloc/test_pmr_adapter.cpp
    containers/test_small_vector.cpp
    containers/test_flat_set.cpp
    containers/test_dense_index.cpp
    containers/test_ordered_map.cpp
    graph/test_id.cpp
    graph/test_attribute.cpp
    threads/test_pool.cpp
    threads/test_mpmc.cpp
    threads/test_work_stealing.cpp
    io/test_mmap.cpp
    io/test_ppm.cpp
    io/test_obj.cpp
    scene/test_handle32.cpp
    scene/test_sphere_soa.cpp
    scene/test_quad_tri_soa.cpp
    scene/test_material_soa.cpp
    scene/test_scene.cpp
    scene/test_bvh_build.cpp
    scene/test_hit.cpp
    render/test_camera.cpp
    render/test_film.cpp
    render/test_tonemap.cpp
    render/test_material_scatter.cpp
    render/test_ray_color.cpp
    render/test_path_trace.cpp
    render/test_sw_clip.cpp
    render/test_sw_span_buffer.cpp
    render/test_sw_rasterize.cpp
    render/test_sw_lightmap.cpp
    render/test_sw_rasterize_full.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads aleph_io
    aleph_types
    aleph_scene aleph_render_common aleph_render_rt aleph_render_sw)
add_test(NAME aleph_tests COMMAND aleph_tests)

if(ALEPH_HAVE_SDL2)
    target_sources(aleph_tests PRIVATE
        window/test_window_init.cpp
        editor/test_orbit.cpp
        editor/test_pick.cpp)
    target_link_libraries(aleph_tests PRIVATE aleph_window aleph_editor)
endif()

add_subdirectory(isolation)
