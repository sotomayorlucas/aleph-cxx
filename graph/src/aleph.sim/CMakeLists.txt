add_library(aleph_sim)
target_sources(aleph_sim
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.sim.cppm
        aleph.sim-section.cppm
        aleph.sim-wave.cppm
        aleph.sim-diffuse.cppm)
target_link_libraries(aleph_sim
    # aleph_linalg_sparse is PUBLIC: DMatrix appears in WaveStepper's exported
    # signature (step/cfl_ok), so it is part of aleph.sim's public interface.
    PUBLIC  aleph_types aleph_math aleph_linalg_sparse
    PRIVATE aleph_flags_isa)
