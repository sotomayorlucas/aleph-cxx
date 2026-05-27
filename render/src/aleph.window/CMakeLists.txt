add_library(aleph_window)
target_sources(aleph_window
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.window.cppm
        aleph.window-event.cppm
        aleph.window-window.cppm)
target_link_libraries(aleph_window
    PUBLIC  aleph_math
    PRIVATE aleph_flags_isa)
if(TARGET SDL2::SDL2)
    target_link_libraries(aleph_window PUBLIC SDL2::SDL2)
elseif(TARGET PkgConfig::SDL2)
    target_link_libraries(aleph_window PUBLIC PkgConfig::SDL2)
else()
    target_include_directories(aleph_window PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(aleph_window PUBLIC ${SDL2_LIBRARIES})
endif()
