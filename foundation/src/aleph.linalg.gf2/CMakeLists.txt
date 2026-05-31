add_library(aleph_linalg_gf2)
target_sources(aleph_linalg_gf2
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.linalg.gf2.cppm
        aleph.linalg.gf2-bitvec.cppm
        aleph.linalg.gf2-bitmatrix.cppm)
target_link_libraries(aleph_linalg_gf2
    PRIVATE aleph_flags_isa)
