add_library(aleph_render_rt)
target_sources(aleph_render_rt
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.rt.cppm
        aleph.render.rt-sampling.cppm
        aleph.render.rt-material.cppm
        aleph.render.rt-integrator.cppm)
target_link_libraries(aleph_render_rt
    PUBLIC  aleph_scene aleph_render_common aleph_math aleph_threads
    PRIVATE aleph_flags_isa)
