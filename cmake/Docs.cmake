# 'docs' target: Doxygen HTML for the headers, the README and docs/, if Doxygen is installed.
#   cmake --build --preset clang-debug --target docs
# Output: build/<preset>/docs/html/index.html. Not part of 'all'.

find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found; the 'docs' target is unavailable")
    return()
endif()

set(DOXYGEN_PROJECT_NAME "wxLife")
set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
set(DOXYGEN_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/docs)
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ${PROJECT_SOURCE_DIR}/README.md)
set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
set(DOXYGEN_FULL_PATH_NAMES YES)        # these three make class pages show #include "wxLife/core/World.h"
set(DOXYGEN_STRIP_FROM_INC_PATH ${PROJECT_SOURCE_DIR}/include)
set(DOXYGEN_FORCE_LOCAL_INCLUDES YES)
set(DOXYGEN_EXTRACT_ALL YES)            # list every entity without forcing comments on obvious ones
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)    # documentation is selective on purpose
set(DOXYGEN_WARN_IF_INCOMPLETE_DOC NO)  # so is @param: only where it adds information
set(DOXYGEN_EXTENSION_MAPPING "h=C++")  # headers are .h, but always C++
set(DOXYGEN_FILE_PATTERNS *.h *.md *.dox)
set(DOXYGEN_GENERATE_TREEVIEW YES)
set(DOXYGEN_QUIET YES)
if(DOXYGEN_DOT_FOUND)
    set(DOXYGEN_HAVE_DOT YES)
    set(DOXYGEN_INCLUDE_GRAPH YES)      # makes the layering visible
    set(DOXYGEN_CLASS_GRAPH YES)
    set(DOXYGEN_CALL_GRAPH NO)
endif()

doxygen_add_docs(docs
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/README.md
    ${PROJECT_SOURCE_DIR}/docs
    COMMENT "Generating Doxygen HTML")
