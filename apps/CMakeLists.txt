# Executables added in later tasks.
add_subdirectory(aleph_rt)
if(ALEPH_HAVE_SDL2)
    add_subdirectory(aleph_sw)
endif()
