# Script mode (cmake -P), driven by the 'coverage' target in Coverage.cmake.
# Runs ctest with LLVM_PROFILE_FILE set, merges the raw profiles, then prints and writes the report.

foreach(var BUILD_DIR SOURCE_DIR BINARIES IGNORE_REGEX LLVM_PROFDATA LLVM_COV CTEST)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "RunCoverage.cmake requires -D${var}=...")
    endif()
endforeach()
string(REPLACE "\\;" ";" BINARIES "${BINARIES}")

set(out_dir "${BUILD_DIR}/coverage")
set(raw_dir "${out_dir}/raw")
set(profdata "${out_dir}/merged.profdata")
file(REMOVE_RECURSE "${out_dir}")
file(MAKE_DIRECTORY "${raw_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "LLVM_PROFILE_FILE=${raw_dir}/%p-%m.profraw"
            "${CTEST}" --test-dir "${BUILD_DIR}" --output-on-failure
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Tests failed while collecting coverage")
endif()

file(GLOB profiles "${raw_dir}/*.profraw")
if(NOT profiles)
    message(FATAL_ERROR "No .profraw files were written to ${raw_dir}")
endif()
execute_process(
    COMMAND "${LLVM_PROFDATA}" merge -sparse ${profiles} -o "${profdata}"
    COMMAND_ERROR_IS_FATAL ANY)

# llvm-cov takes the first binary positionally and the rest as -object.
list(POP_FRONT BINARIES first_binary)
set(object_args "")
foreach(binary IN LISTS BINARIES)
    list(APPEND object_args -object "${binary}")
endforeach()
set(common_args "${first_binary}" ${object_args} "-instr-profile=${profdata}"
                "-ignore-filename-regex=${IGNORE_REGEX}")

execute_process(
    COMMAND "${LLVM_COV}" report ${common_args} "${SOURCE_DIR}"
    OUTPUT_VARIABLE report
    COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${out_dir}/summary.txt" "${report}")
message("${report}")

execute_process(
    COMMAND "${LLVM_COV}" show ${common_args} -format=html "-output-dir=${out_dir}/html"
            -show-line-counts-or-regions -show-branches=count -Xdemangler c++filt "${SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Coverage HTML: ${out_dir}/html/index.html")
