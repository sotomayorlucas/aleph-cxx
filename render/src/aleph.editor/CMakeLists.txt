add_library(aleph_editor)
target_sources(aleph_editor
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.editor.cppm
        aleph.editor-font.cppm
        aleph.editor-ui.cppm
        aleph.editor-orbit.cppm
        aleph.editor-picking.cppm
        aleph.editor-perf.cppm)
target_link_libraries(aleph_editor
    PUBLIC  aleph_render_common aleph_render_sw aleph_window aleph_math
    PRIVATE aleph_flags_isa)
