# Script mode (cmake -P), driven by the 'static_link' test in tests/CMakeLists.txt:
#   cmake -DEXECUTABLE=<path> -P CheckStaticLink.cmake
# Fails unless the platform's dependency lister runs successfully and lists no shared wxWidgets library:
# ldd on Linux (libwx_*.so), otool -L on macOS (libwx_*.dylib) and dumpbin /dependents on Windows
# (wxbase*.dll, wxmsw*.dll; dumpbin comes with MSVC, in the environment the build runs in).
# (A plain "! ldd ... | grep -q libwx_" would also pass when the executable is missing.)
if(CMAKE_HOST_WIN32)
    find_program(DUMPBIN_PROGRAM dumpbin)
    if(NOT DUMPBIN_PROGRAM)
        message(FATAL_ERROR "dumpbin not found: run the tests from a Developer PowerShell for VS")
    endif()
    set(lister ${DUMPBIN_PROGRAM} /dependents)
elseif(CMAKE_HOST_APPLE)
    set(lister otool -L)
else()
    set(lister ldd)
endif()

execute_process(COMMAND ${lister} "${EXECUTABLE}" OUTPUT_VARIABLE deps ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    list(JOIN lister " " command)
    message(FATAL_ERROR "${command} failed for ${EXECUTABLE}: ${err}")
endif()
if(deps MATCHES "libwx_|wx(base|msw)[0-9]")
    message(FATAL_ERROR "wxLife links wxWidgets dynamically:\n${deps}")
endif()
