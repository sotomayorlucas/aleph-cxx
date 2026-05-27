cmake_minimum_required(VERSION 3.28)
project(aleph_cxx
    VERSION 0.1.0
    DESCRIPTION "aleph-cxx — cycle-tight C++26 engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/aleph_flags.cmake)

add_subdirectory(foundation)

# ─── SDL2 detection ─────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
    endif()
endif()
if(SDL2_FOUND OR SDL2_LIBRARIES)
    set(ALEPH_HAVE_SDL2 ON)
    message(STATUS "aleph-cxx: SDL2 found — window + editor + aleph_sw will be built.")
else()
    set(ALEPH_HAVE_SDL2 OFF)
    message(STATUS "aleph-cxx: SDL2 NOT found — window + editor + aleph_sw skipped.")
endif()

add_subdirectory(render)
add_subdirectory(apps)

add_subdirectory(bench)

enable_testing()
add_subdirectory(tests)
