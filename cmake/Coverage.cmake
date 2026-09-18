# 'coverage' target: runs the tests with Clang source-based coverage and writes a report.
#   cmake --workflow --preset coverage
# Report: build/coverage/coverage/summary.txt, HTML: build/coverage/coverage/html/index.html

if(NOT WXLIFE_BUILD_TESTS)
    message(FATAL_ERROR "WXLIFE_ENABLE_COVERAGE needs WXLIFE_BUILD_TESTS=ON")
endif()

find_program(LLVM_PROFDATA_PROGRAM llvm-profdata REQUIRED)
find_program(LLVM_COV_PROGRAM llvm-cov REQUIRED)

# Every test executable whose coverage should be counted.
set(coverage_targets wxLife_tests)

set(coverage_binaries "")
foreach(target IN LISTS coverage_targets)
    list(APPEND coverage_binaries "$<TARGET_FILE:${target}>")
endforeach()
list(JOIN coverage_binaries "\\;" coverage_binaries_arg)

add_custom_target(coverage
    COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARIES=${coverage_binaries_arg}"
        "-DIGNORE_REGEX=.*/(build|_deps|tests)/.*"
        "-DLLVM_PROFDATA=${LLVM_PROFDATA_PROGRAM}"
        "-DLLVM_COV=${LLVM_COV_PROGRAM}"
        "-DCTEST=${CMAKE_CTEST_COMMAND}"
        -P ${PROJECT_SOURCE_DIR}/cmake/RunCoverage.cmake
    DEPENDS ${coverage_targets}
    USES_TERMINAL
    VERBATIM
    COMMENT "Running tests and generating the coverage report")
if(TARGET wxLife)
    add_dependencies(coverage wxLife)   # the static_link test inspects the app
endif()
