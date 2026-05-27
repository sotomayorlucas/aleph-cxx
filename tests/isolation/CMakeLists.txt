# Each iso_X.cpp is built into its own tiny executable that imports
# exactly ONE module. If a future change makes that module pull in a
# cross-module dep that isn't declared in CMake, this target fails to
# link. CTest verifies they all run cleanly.

function(aleph_iso_test name link)
    add_executable(iso_${name} iso_${name}.cpp)
    target_link_libraries(iso_${name} PRIVATE ${link} aleph_flags_test)
    add_test(NAME iso_${name} COMMAND iso_${name})
endfunction()

aleph_iso_test(cpu        aleph_cpu)
aleph_iso_test(math       aleph_math)
aleph_iso_test(alloc      aleph_alloc)
aleph_iso_test(containers aleph_containers)
aleph_iso_test(io         aleph_io)
