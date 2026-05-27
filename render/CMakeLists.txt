# Module libraries added in subsequent tasks via add_subdirectory.
add_subdirectory(src/aleph.scene)
add_subdirectory(src/aleph.render.common)
add_subdirectory(src/aleph.render.rt)
add_subdirectory(src/aleph.render.sw)
# if(ALEPH_HAVE_SDL2)
#     add_subdirectory(src/aleph.window)    — added in Task 18
#     add_subdirectory(src/aleph.editor)    — added in Task 19
# endif()
