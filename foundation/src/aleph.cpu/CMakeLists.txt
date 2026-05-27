add_library(aleph_cpu)
target_sources(aleph_cpu
    PUBLIC FILE_SET CXX_MODULES FILES
        aleph.cpu.cppm
        aleph.cpu-isa.cppm
        aleph.cpu-cycles.cppm
        aleph.cpu-hints.cppm)
# Use aleph_flags_isa (not aleph_flags_strict) so the module BMI dialect
# (exceptions/rtti mode) matches test consumers that use aleph_flags_test.
# GCC hard-errors on cross-dialect BMI imports.
target_link_libraries(aleph_cpu PRIVATE aleph_flags_isa)
