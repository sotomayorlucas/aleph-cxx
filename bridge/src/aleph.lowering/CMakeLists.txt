add_library(aleph_lowering)
target_sources(aleph_lowering
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.lowering.cppm
        aleph.lowering-lowered.cppm
        aleph.lowering-lower.cppm
        aleph.lowering-build.cppm
        aleph.lowering-ops.cppm
        aleph.lowering-grouping.cppm
        aleph.lowering-incremental.cppm
        aleph.lowering-importance.cppm
        aleph.lowering-build_sw.cppm)
target_link_libraries(aleph_lowering
    PUBLIC  aleph_graph aleph_types aleph_scene aleph_dpo aleph_math aleph_sheaf aleph_flow aleph_render_sw
    PRIVATE aleph_flags_isa)
