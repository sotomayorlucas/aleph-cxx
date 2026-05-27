add_library(aleph_scene)
target_sources(aleph_scene
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.scene.cppm
        aleph.scene-handle32.cppm
        aleph.scene-sphere_soa.cppm
        aleph.scene-quad_soa.cppm
        aleph.scene-tri_soa.cppm
        aleph.scene-material_soa.cppm
        aleph.scene-scene.cppm)
target_link_libraries(aleph_scene
    PUBLIC  aleph_math aleph_containers aleph_alloc aleph_io
    PRIVATE aleph_flags_isa)
