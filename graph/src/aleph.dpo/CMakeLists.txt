add_library(aleph_dpo)
target_sources(aleph_dpo
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.dpo.cppm
        aleph.dpo-pattern.cppm
        aleph.dpo-rule.cppm
        aleph.dpo-matcher.cppm
        aleph.dpo-apply.cppm
        aleph.dpo-rules.cppm)
target_link_libraries(aleph_dpo
    PUBLIC  aleph_types aleph_graph aleph_containers
    PRIVATE aleph_flags_isa)
