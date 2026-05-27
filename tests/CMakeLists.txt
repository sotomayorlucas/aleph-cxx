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
    math/test_rotor_advanced.cpp)
target_include_directories(aleph_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(aleph_tests PRIVATE
    aleph_flags_test aleph_cpu aleph_math)
add_test(NAME aleph_tests COMMAND aleph_tests)
