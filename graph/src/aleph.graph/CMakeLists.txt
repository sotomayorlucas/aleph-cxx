add_library(aleph_graph)
target_sources(aleph_graph
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.graph.cppm
        aleph.graph-graph.cppm)
target_link_libraries(aleph_graph
    PUBLIC  aleph_types aleph_containers
    PRIVATE aleph_flags_isa)
