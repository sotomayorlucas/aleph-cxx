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

enable_testing()
add_subdirectory(tests)
