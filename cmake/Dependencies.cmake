include(FetchContent)

# FIND_PACKAGE_ARGS: an installed package (find_package) wins; otherwise the source is downloaded.
# SYSTEM: the dependency's headers are system headers, so our warnings and clang-tidy skip them.
# EXCLUDE_FROM_ALL: only the parts of the dependency we link against get built.

find_package(Threads REQUIRED)   # the stepping engine runs row bands on std::jthreads

if(WXLIFE_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.18.0
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS NAMES GTest)
    FetchContent_MakeAvailable(googletest)
endif()

# wxWidgets is always built from source as static libraries (no FIND_PACKAGE_ARGS), so the app needs no
# wxWidgets libraries at run time. Only wx::core, wx::base and what they need are built.
# Offline builds or a shared source tree: -DFETCHCONTENT_SOURCE_DIR_WXWIDGETS=/path/to/wxWidgets-3.2.11
if(WXLIFE_BUILD_UI)
    set(wxBUILD_SHARED OFF CACHE BOOL "Build wx libraries as shared libs" FORCE)   # not negotiable

    # Everything else is an ordinary cache default, so a -D on the command line still wins
    # (wx declares its options without FORCE).
    function(wxLife_wx_default name value)
        if(NOT DEFINED CACHE{${name}})
            set(${name} "${value}" CACHE STRING "Set by wxLife")
        endif()
    endfunction()

    if(UNIX AND NOT APPLE)
        wxLife_wx_default(wxBUILD_TOOLKIT gtk3)
    endif()
    foreach(option wxBUILD_SAMPLES wxBUILD_TESTS wxBUILD_DEMOS wxBUILD_BENCHMARKS wxBUILD_INSTALL
                   wxBUILD_LOCALES wxBUILD_MONOLITHIC)
        wxLife_wx_default(${option} OFF)
    endforeach()
    # Features wxLife does not use. SPELLCHECK OFF also drops the libgspell dependency, and XTEST OFF drops
    # libXtst (only wxUIActionSimulator needs it).
    foreach(feature WEBVIEW MEDIACTRL STC RICHTEXT PROPGRID RIBBON XRC AUI OPENGL LIBSDL LIBNOTIFY
                    WEBREQUEST SECRETSTORE LIBTIFF LIBJPEG GTKPRINT LIBMSPACK SPELLCHECK XTEST)
        wxLife_wx_default(wxUSE_${feature} OFF)
    endforeach()
    # zlib, libpng, expat and regex (PCRE2) stay at wx's default "sys": GTK already loads the system copies,
    # and bundled copies could clash with them (wx's bundled zlib also fails to compile with GCC 14 or newer).

    FetchContent_Declare(wxWidgets
        URL      https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.11/wxWidgets-3.2.11.tar.bz2
        URL_HASH SHA256=6a129015bce2e914e4bf61ec4411854ad962801d47e92f2eb8340adb6a90af08
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SYSTEM
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(wxWidgets)   # provides wx::core and wx::base

    # Make sure the static wx was really used: FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS or a dependency
    # provider can supply an installed (shared) wxWidgets instead of the fetched one.
    block()
        foreach(lib wx::core wx::base)
            if(TARGET ${lib})
                get_target_property(type ${lib} TYPE)
                get_target_property(imported ${lib} IMPORTED)
            endif()
            if(NOT TARGET ${lib} OR imported OR NOT type STREQUAL "STATIC_LIBRARY")
                message(FATAL_ERROR "wxLife needs ${lib} as a static library built from the fetched "
                    "wxWidgets sources, but an installed or shared wxWidgets was used. Do not set "
                    "FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS for this build; to build from a local "
                    "wxWidgets tree, set FETCHCONTENT_SOURCE_DIR_WXWIDGETS instead.")
            endif()
        endforeach()
    endblock()
endif()

# Adding another dependency (then link fmt::fmt):
#
# FetchContent_Declare(fmt
#     GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#     GIT_TAG        12.2.0
#     GIT_SHALLOW    TRUE
#     SYSTEM
#     EXCLUDE_FROM_ALL
#     FIND_PACKAGE_ARGS)
# FetchContent_MakeAvailable(fmt)
