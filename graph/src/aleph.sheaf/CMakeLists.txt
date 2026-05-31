add_library(aleph_sheaf)
target_sources(aleph_sheaf
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.sheaf.cppm
        aleph.sheaf-simplex.cppm
        aleph.sheaf-union_find.cppm
        aleph.sheaf-skeleton.cppm
        aleph.sheaf-flag_complex.cppm
        aleph.sheaf-subgraph.cppm
        aleph.sheaf-sheaf_trait.cppm
        aleph.sheaf-sheaf_constant.cppm
        aleph.sheaf-sheaf_visibility.cppm
        aleph.sheaf-sheaf_spd.cppm
        aleph.sheaf-cochain.cppm
        aleph.sheaf-cohomology.cppm
        aleph.sheaf-connecting.cppm
        aleph.sheaf-dec.cppm
        aleph.sheaf-mayer_vietoris.cppm
        aleph.sheaf-zigzag.cppm)
target_link_libraries(aleph_sheaf
    PUBLIC  aleph_graph aleph_types aleph_containers aleph_linalg_gf2 aleph_math
    PRIVATE aleph_flags_isa)
