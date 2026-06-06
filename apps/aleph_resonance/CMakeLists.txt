add_executable(aleph_resonance main.cpp)
target_link_libraries(aleph_resonance PRIVATE
    aleph_graph aleph_types aleph_flow aleph_sim
    aleph_math aleph_alloc aleph_containers aleph_sheaf aleph_dpo
    aleph_linalg_sparse
    aleph_flags_test)
