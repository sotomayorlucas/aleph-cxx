add_library(aleph_containers)
target_sources(aleph_containers
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.containers.cppm
        aleph.containers-small_vector.cppm
        aleph.containers-flat_set.cppm
        aleph.containers-dense_index.cppm)
target_link_libraries(aleph_containers
    PUBLIC  aleph_alloc
    PRIVATE aleph_flags_isa)
