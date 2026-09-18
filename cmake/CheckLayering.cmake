# Script mode (cmake -P), driven by the 'layering' test in tests/CMakeLists.txt:
#   cmake -DSOURCE_DIR=<repo> -P CheckLayering.cmake
# Fails if a layer includes wx (core, render) or a layer above it (see docs/architecture.md). Both the
# headers (include/wxLife/<layer>) and the sources (src/<layer>) are checked. Tests may include any layer.
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "CheckLayering.cmake requires -DSOURCE_DIR=...")
endif()

set(include "#[ \t]*include[ \t]*[<\"]")   # '#include', '# include', <...> and "..."
set(forbidden_core   "${include}wx/" "${include}wxLife/(render|ui|app)/")
set(forbidden_render "${include}wx/" "${include}wxLife/(ui|app)/")
set(forbidden_ui     "${include}wxLife/app/")
set(violations "")
foreach(layer core render ui)
    file(GLOB_RECURSE files
        "${SOURCE_DIR}/include/wxLife/${layer}/*.h"
        "${SOURCE_DIR}/src/${layer}/*.cpp")
    if(NOT files)
        message(FATAL_ERROR "No sources found for layer '${layer}' below ${SOURCE_DIR}")
    endif()
    foreach(file IN LISTS files)
        foreach(pattern IN LISTS forbidden_${layer})
            file(STRINGS "${file}" hits REGEX "${pattern}")
            if(hits)
                list(APPEND violations "${file}: ${hits}")
            endif()
        endforeach()
    endforeach()
endforeach()
if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Layering violations:\n  ${text}")
endif()
