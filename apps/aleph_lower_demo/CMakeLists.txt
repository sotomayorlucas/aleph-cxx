add_executable(aleph_lower_demo main.cpp)
target_link_libraries(aleph_lower_demo PRIVATE
    aleph_lowering aleph_graph aleph_types
    aleph_flags_test)
