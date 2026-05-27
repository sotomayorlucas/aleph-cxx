add_library(aleph_alloc)
target_sources(aleph_alloc
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.alloc.cppm
        aleph.alloc-arena.cppm)
target_link_libraries(aleph_alloc
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_isa)
