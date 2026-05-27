add_library(aleph_render_common)
target_sources(aleph_render_common
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.render.common.cppm
        aleph.render.common-camera.cppm
        aleph.render.common-sky.cppm
        aleph.render.common-film.cppm
        aleph.render.common-tonemap.cppm)
target_link_libraries(aleph_render_common
    PUBLIC  aleph_math aleph_alloc
    PRIVATE aleph_flags_isa)
