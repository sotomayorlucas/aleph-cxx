# INTERFACE library carrying common compile options. Each aleph target
# links to one of these depending on its purpose.

add_library(aleph_flags_common INTERFACE)
target_compile_options(aleph_flags_common INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
    -Wformat=2 -Wmissing-declarations -Wsign-conversion
    -fdiagnostics-color=always)

add_library(aleph_flags_strict INTERFACE)
target_compile_options(aleph_flags_strict INTERFACE
    -march=x86-64-v3 -mavx2 -mfma -mbmi2
    -fno-rtti -fno-exceptions
    -fno-plt -fno-semantic-interposition
    -fno-stack-protector
    -fvisibility=hidden -fvisibility-inlines-hidden
    -funroll-loops -fpeel-loops -ftree-vectorize)
target_link_libraries(aleph_flags_strict INTERFACE aleph_flags_common)

# ISA + optimisation flags without ABI-breaking -fno-exceptions/-fno-rtti.
# Use for C++ module libraries whose BMI will be imported by test binaries
# (which need exceptions for doctest). GCC tags BMI dialect with exception/
# rtti mode and refuses cross-dialect imports.
add_library(aleph_flags_isa INTERFACE)
target_compile_options(aleph_flags_isa INTERFACE
    -march=x86-64-v3 -mavx2 -mfma -mbmi2
    -fno-plt -fno-semantic-interposition
    -fno-stack-protector
    -fvisibility=hidden -fvisibility-inlines-hidden
    -funroll-loops -fpeel-loops -ftree-vectorize)
target_link_libraries(aleph_flags_isa INTERFACE aleph_flags_common)

add_library(aleph_flags_test INTERFACE)
# tests need exceptions for doctest; keep ISA strict so SIMD code paths
# behave identically to production.
target_compile_options(aleph_flags_test INTERFACE
    -march=x86-64-v3 -mavx2 -mfma -mbmi2)
target_link_libraries(aleph_flags_test INTERFACE aleph_flags_common)
