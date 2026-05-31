add_library(aleph_flow)
target_sources(aleph_flow
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.flow.cppm
        aleph.flow-wasserstein1.cppm
        aleph.flow-wasserstein2.cppm
        aleph.flow-ollivier_ricci.cppm
        aleph.flow-laplacian.cppm
        aleph.flow-flow.cppm)
target_link_libraries(aleph_flow
    PUBLIC  aleph_graph aleph_types aleph_containers aleph_linalg_sparse aleph_sheaf aleph_dpo aleph_math
    PRIVATE aleph_flags_isa)
