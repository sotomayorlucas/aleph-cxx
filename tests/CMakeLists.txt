add_executable(aleph_tests
    test_main.cpp
    test_smoke.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE aleph_flags_test)
add_test(NAME aleph_tests COMMAND aleph_tests)
