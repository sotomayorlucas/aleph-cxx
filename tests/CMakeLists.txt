add_executable(aleph_tests
    tla_cxx_sync.cpp
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
    graph/test_node.cpp
    graph/test_edge.cpp
    graph/test_graph_basic.cpp
    graph/test_graph_edges.cpp
    graph/test_invariants_1_5.cpp
    graph/test_invariants_6_10.cpp
    linalg/test_gf2.cpp
    linalg/test_sparse.cpp
    dpo/test_dpo.cpp
    sheaf/test_simplex.cpp
    sheaf/test_union_find.cpp
    sheaf/test_skeleton.cpp
    sheaf/test_flag_complex.cpp
    sheaf/test_subgraph.cpp
    sheaf/test_sheaf_trait.cpp
    sheaf/test_sheaf_constant.cpp
    sheaf/test_sheaf_visibility.cpp
    sheaf/test_sheaf_spd.cpp
    sheaf/test_cochain.cpp
    sheaf/test_cohomology.cpp
    sheaf/test_connecting.cpp
    sheaf/test_dec.cpp
    sheaf/test_mayer_vietoris.cpp
    sheaf/test_zigzag.cpp
    flow/test_wasserstein1.cpp
    flow/test_wasserstein2.cpp
    flow/test_ollivier_ricci.cpp
    flow/test_laplacian.cpp
    flow/test_mv_localization.cpp
    flow/test_flow.cpp
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
    render/test_sw_rasterize_full.cpp
    render/test_sw_downsample.cpp
    render/test_grouped_nee.cpp
    render/test_adaptive_spp.cpp
    lowering/test_lower_minimal.cpp
    lowering/test_transform_hierarchy.cpp
    lowering/test_build_render_scene.cpp
    lowering/test_missing_reference.cpp
    lowering/test_edit_material.cpp
    lowering/test_add_object.cpp
    lowering/test_dpo_edit.cpp
    lowering/test_determinism.cpp
    lowering/test_light_grouping.cpp
    lowering/test_groups_in_scene.cpp
    lowering/test_incremental.cpp
    lowering/test_importance.cpp
    edit/test_build_sw.cpp
    edit/test_controller.cpp
    edit/test_sim_controller.cpp
    edit/test_mv_controller.cpp
    sim/test_section.cpp
    sim/test_wave.cpp
    sim/test_diffuse.cpp)
target_include_directories(aleph_tests SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lowering)  # shared lowering_freeze.hpp (used by edit/test_controller.cpp)
target_compile_definitions(aleph_tests PRIVATE
    ALEPH_TLA_SCENE_GRAPH_PATH="${CMAKE_SOURCE_DIR}/formal/scene_graph.tla")
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math aleph_alloc aleph_containers aleph_threads aleph_io
    aleph_types aleph_graph aleph_dpo aleph_sheaf aleph_flow aleph_sim
    aleph_linalg_gf2 aleph_linalg_sparse
    aleph_scene aleph_render_common aleph_render_rt aleph_render_sw
    aleph_lowering aleph_edit)
add_test(NAME aleph_tests COMMAND aleph_tests)

if(ALEPH_HAVE_SDL2)
    target_sources(aleph_tests PRIVATE
        window/test_window_init.cpp
        editor/test_orbit.cpp
        editor/test_pick.cpp)
    target_link_libraries(aleph_tests PRIVATE aleph_window aleph_editor)
endif()

add_subdirectory(isolation)
