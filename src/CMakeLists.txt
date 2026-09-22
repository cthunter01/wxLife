# Layers, lowest first; each one links only the layers below it (see docs/architecture.md).
#   wxLife_lib  core/ and render/: the simulation and its pixels. Standard library and threads, never wx.
#   wxLife_ui   ui/ and app/: the wxWidgets windows, input and timer, and the wxApp.
#   wxLife      Main.cpp: the executable.

add_library(wxLife_lib STATIC
    core/BandedStepper.cpp core/Format.cpp core/Grid.cpp core/Pacer.cpp core/ParallelBands.cpp
    core/ReferenceStepper.cpp core/Rule.cpp core/Speed.cpp core/StepKernel.cpp core/Stepper.cpp
    core/World.cpp core/WorldLimits.cpp
    render/PixelBuffer.cpp render/Rasterizer.cpp render/Viewport.cpp)
add_library(wxLife::lib ALIAS wxLife_lib)
target_include_directories(wxLife_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wxLife_lib PUBLIC Threads::Threads)
wxLife_configure_target(wxLife_lib)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The stepping loop and the rasterizer are optimised in every configuration, so Debug builds stay
    # interactive: unoptimised, a maximised 4K canvas takes up to 90 ms per frame. Not with MSVC, whose
    # Debug runtime checks (/RTC1) cannot be combined with /O2.
    set_source_files_properties(core/StepKernel.cpp render/Rasterizer.cpp PROPERTIES COMPILE_OPTIONS -O3)
endif()
if(WXLIFE_KERNEL_CLONES)
    set_source_files_properties(core/StepKernel.cpp PROPERTIES COMPILE_DEFINITIONS WXLIFE_KERNEL_CLONES)
endif()

if(NOT WXLIFE_BUILD_UI)
    return()
endif()

add_library(wxLife_ui STATIC
    app/LifeApp.cpp
    ui/CommandIds.cpp ui/ControlPanel.cpp ui/MainFrame.cpp ui/MenuBar.cpp ui/SimulationRunner.cpp
    ui/WorldCanvas.cpp ui/WorldSizeDialog.cpp)
add_library(wxLife::ui ALIAS wxLife_ui)
target_link_libraries(wxLife_ui PUBLIC wxLife::lib wx::core wx::base)
target_compile_definitions(wxLife_ui PRIVATE WXLIFE_VERSION="${PROJECT_VERSION}")
wxLife_configure_target(wxLife_ui)

add_executable(wxLife Main.cpp)
target_link_libraries(wxLife PRIVATE wxLife::ui)
wxLife_configure_target(wxLife)
# A GUI program on every platform. Windows: the GUI subsystem, whose entry point is the WinMain that
# wxIMPLEMENT_APP defines (it defines no main()). macOS: an application bundle, bin/wxLife.app, with
# the Info.plist template that wxWidgets uses for its own programs.
set_target_properties(wxLife PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${wxwidgets_SOURCE_DIR}/src/osx/carbon/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME wxLife
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cthunter01.wxLife
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_STRING "wxLife ${PROJECT_VERSION}")
if(WXLIFE_STATIC_LIBSTDCXX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "WXLIFE_STATIC_LIBSTDCXX is for Linux (libstdc++ and libgcc)")
    endif()
    target_link_options(wxLife PRIVATE -static-libstdc++ -static-libgcc)
endif()

include(GNUInstallDirs)
install(TARGETS wxLife RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
