cmake_minimum_required(VERSION 3.28)   # FetchContent SYSTEM + EXCLUDE_FROM_ALL + FIND_PACKAGE_ARGS

# macOS: the oldest macOS the app runs on, for every target including wxWidgets. It must be set before
# project(). Without it wxWidgets sets its own minimum, 10.10, for the whole build, and libc++ has no
# floating-point std::format or std::to_chars before 13.3. A -D on the command line wins; other platforms
# ignore it.
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3 CACHE STRING "Minimum macOS version")

# C: wxWidgets compiles some C sources, and the presets pick the C compiler too.
project(wxLife VERSION 0.1.0 DESCRIPTION "Conway's Game of Life and Langton's ant with wxWidgets"
        LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset clang-debug")
endif()

# Presets set the build type; this covers a bare `cmake -B build`. A parent project keeps its own choice.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)     # no C++ modules; remove this line to use them
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

option(WXLIFE_BUILD_UI "Build the wx application (downloads and builds wxWidgets)" ON)
option(WXLIFE_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(WXLIFE_BUILD_BENCH "Build the headless stepping benchmark" ${PROJECT_IS_TOP_LEVEL})
option(WXLIFE_BUILD_DOCS "Add a 'docs' target when Doxygen is found" ${PROJECT_IS_TOP_LEVEL})
option(WXLIFE_KERNEL_CLONES
       "Build the stepping kernel for AVX2 and baseline x86-64 (runtime dispatch; x86-64 Linux only)" ON)
option(WXLIFE_WARNINGS_AS_ERRORS "Treat compiler and clang-tidy warnings as errors" OFF)
option(WXLIFE_ENABLE_COVERAGE "Clang source-based coverage and a 'coverage' target" OFF)
option(WXLIFE_ENABLE_CLANG_TIDY "Run clang-tidy on our sources while compiling" OFF)
option(WXLIFE_ENABLE_IPO "Link-time optimisation for our targets" OFF)
option(WXLIFE_STATIC_LIBSTDCXX "Also link libstdc++ and libgcc statically into the app (Linux)" OFF)
set(WXLIFE_SANITIZERS "" CACHE STRING "Sanitizers for our targets, e.g. address;undefined or thread")

find_program(CCACHE_PROGRAM ccache)
foreach(lang C CXX)
    if(CCACHE_PROGRAM AND NOT CMAKE_${lang}_COMPILER_LAUNCHER)   # a launcher the user chose wins
        set(CMAKE_${lang}_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()
endforeach()

# Point clangd (in any editor) at the most recently configured preset.
# Windows allows symlinks only in Developer Mode (or as admin), so a failure is a note, not an error.
if(PROJECT_IS_TOP_LEVEL)
    file(CREATE_LINK ${PROJECT_BINARY_DIR}/compile_commands.json
         ${PROJECT_SOURCE_DIR}/compile_commands.json RESULT link_result SYMBOLIC)
    if(NOT link_result EQUAL 0)
        message(STATUS "Could not link compile_commands.json into the source tree (${link_result}). "
                       "On Windows, turn on Developer Mode to allow symlinks.")
    endif()
endif()

# Project modules are included by path, so a parent's CMAKE_MODULE_PATH cannot replace them.
include(${PROJECT_SOURCE_DIR}/cmake/ProjectOptions.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Dependencies.cmake)

add_subdirectory(src)
if(WXLIFE_BUILD_BENCH)
    add_subdirectory(tools/bench)
endif()

if(WXLIFE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(WXLIFE_ENABLE_COVERAGE)
    include(${PROJECT_SOURCE_DIR}/cmake/Coverage.cmake)
endif()
if(WXLIFE_BUILD_DOCS)
    include(${PROJECT_SOURCE_DIR}/cmake/Docs.cmake)
endif()
