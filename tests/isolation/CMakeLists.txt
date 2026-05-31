# Each iso_X.cpp is built into its own tiny executable that imports
# exactly ONE module. If a future change makes that module pull in a
# cross-module dep that isn't declared in CMake, this target fails to
# link. CTest verifies they all run cleanly.

function(aleph_iso_test name link)
    add_executable(iso_${name} iso_${name}.cpp)
    target_link_libraries(iso_${name} PRIVATE ${link} aleph_flags_test)
    add_test(NAME iso_${name} COMMAND iso_${name})
endfunction()

aleph_iso_test(cpu        aleph_cpu)
aleph_iso_test(math       aleph_math)
aleph_iso_test(alloc      aleph_alloc)
aleph_iso_test(containers aleph_containers)
aleph_iso_test(io         aleph_io)

aleph_iso_test(scene          aleph_scene)
aleph_iso_test(render_common  aleph_render_common)
aleph_iso_test(render_rt      aleph_render_rt)
aleph_iso_test(render_sw      aleph_render_sw)

if(ALEPH_HAVE_SDL2)
    aleph_iso_test(window aleph_window)
    aleph_iso_test(editor aleph_editor)
endif()

aleph_iso_test(types aleph_types)
aleph_iso_test(graph aleph_graph)
aleph_iso_test(dpo   aleph_dpo)

aleph_iso_test(linalg_gf2    aleph_linalg_gf2)
aleph_iso_test(linalg_sparse aleph_linalg_sparse)
