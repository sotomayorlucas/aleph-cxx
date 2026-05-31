add_library(aleph_linalg_sparse)
target_sources(aleph_linalg_sparse
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.linalg.sparse.cppm
        aleph.linalg.sparse-dense.cppm
        aleph.linalg.sparse-csr.cppm
        aleph.linalg.sparse-ldlt.cppm
        aleph.linalg.sparse-ldlt_sparse.cppm)
target_link_libraries(aleph_linalg_sparse
    PUBLIC  aleph_math
    PRIVATE aleph_flags_isa)
