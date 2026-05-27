add_library(aleph_threads)
target_sources(aleph_threads
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.threads.cppm
        aleph.threads-pool.cppm
        aleph.threads-mpmc.cppm)
find_package(Threads REQUIRED)
target_link_libraries(aleph_threads
    PUBLIC  aleph_alloc aleph_containers Threads::Threads
    PRIVATE aleph_flags_isa)
