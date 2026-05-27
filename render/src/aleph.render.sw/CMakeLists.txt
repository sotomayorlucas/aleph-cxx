add_library(aleph_render_sw)
target_sources(aleph_render_sw
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.sw.cppm
        aleph.render.sw-clip.cppm
        aleph.render.sw-scene_rt.cppm)
target_link_libraries(aleph_render_sw
    PUBLIC  aleph_render_common aleph_math aleph_threads
    PRIVATE aleph_flags_isa)
