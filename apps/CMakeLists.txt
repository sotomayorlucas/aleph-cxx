# Executables added in later tasks.
add_subdirectory(aleph_rt)
add_subdirectory(aleph_graph_fixture)
if(ALEPH_HAVE_SDL2)
    add_subdirectory(aleph_sw)
endif()
