add_library(aleph_io)
target_sources(aleph_io
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.io.cppm
        aleph.io-mmap.cppm
        aleph.io-ppm.cppm
        aleph.io-obj.cppm)
target_link_libraries(aleph_io
    PUBLIC  aleph_cpu
    PRIVATE aleph_flags_isa)
