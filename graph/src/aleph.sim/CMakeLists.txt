add_library(aleph_sim)
target_sources(aleph_sim
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.sim.cppm
        aleph.sim-field.cppm
        aleph.sim-wave.cppm)
target_link_libraries(aleph_sim
    PUBLIC  aleph_types aleph_math
    PRIVATE aleph_linalg_sparse aleph_flags_isa)
