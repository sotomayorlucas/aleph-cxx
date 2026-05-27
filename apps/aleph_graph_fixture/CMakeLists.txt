add_executable(aleph_graph_fixture main.cpp)
target_link_libraries(aleph_graph_fixture PRIVATE
    aleph_types aleph_graph aleph_containers aleph_alloc
    aleph_flags_test)
