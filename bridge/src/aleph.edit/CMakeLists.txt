add_library(aleph_edit)
target_sources(aleph_edit
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.edit.cppm
        aleph.edit-controller.cppm)
# aleph.edit is the sanctioned cross-cutter: it may know all backends.
# Headless — does NOT link aleph_editor or aleph_window.
target_link_libraries(aleph_edit
    PUBLIC  aleph_lowering aleph_scene aleph_render_sw aleph_render_rt aleph_render_common
            aleph_graph aleph_types aleph_dpo aleph_math aleph_threads
            aleph_flow aleph_sim
    PRIVATE aleph_flags_isa)
