add_library(aleph_types)
target_sources(aleph_types
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.types.cppm)
# Populated by Tasks 3-8.
target_link_libraries(aleph_types PRIVATE aleph_flags_isa)
