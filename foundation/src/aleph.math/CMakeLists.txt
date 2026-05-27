add_library(aleph_math)
target_sources(aleph_math
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.math.cppm
        aleph.math-types.cppm
        aleph.math-concepts.cppm
        aleph.math-vec.cppm
        aleph.math-mat.cppm
        aleph.math-quat.cppm
        aleph.math-rotor.cppm
        aleph.math-bivector.cppm
        aleph.math-multivector.cppm)
target_link_libraries(aleph_math
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_isa)
