# Layers, lowest first; each one links only the layers below it (see docs/architecture.md).
#   wxLife_lib  core/ and render/: the simulation and its pixels. Standard library and threads, never wx.
#   wxLife_ui   ui/ and app/: the wxWidgets windows, input and timer, and the wxApp.
#   wxLife      Main.cpp: the executable.

# The demo patterns (see core/Demo.cpp) are built into wxLife_lib as char arrays by
# cmake/EmbedPatterns.cmake, so the program needs no data files next to it on any platform. The generated
# source is rewritten only when a pattern file changes. The giant macrocell patterns stay gzip-compressed
# (.mc.gz) and are unpacked when loaded.
set(demo_patterns
    acorn.rle b52bomber.rle backrake1.rle bigun.rle blinkerpuffer1.rle breeder1.rle canadagoose.rle
    copperhead.rle diehard.rle glider.rle gosperglidergun.rle gosperglidergun_synth.rle gunstar.rle
    koksgalaxy.rle lidka.rle loafer.rle lwss.rle pentadecathlon.rle pp8primecalculator.rle primer.rle
    puffer-train.rle pulsar.rle queenbeeshuttle.rle rabbits.rle rileysbreeder.rle rpentomino.rle
    simkinglidergun.rle spacerake.rle spaceshiprace.rle spider-rake.rle spider.rle
    switch-engine-breeder.rle tumbler.rle turingmachine.rle twinbeesshuttle.rle twinprimecalculator.rle
    universalturingmachine.rle weekender.rle
    caterpillar.mc.gz centipede.mc.gz demonoid-c512-hashlife-friendly.mc.gz gemini.mc.gz
    metapixel-galaxy.mc.gz picalculator.mc.gz succ.mc.gz)
list(TRANSFORM demo_patterns PREPEND ${PROJECT_SOURCE_DIR}/patterns/)
list(JOIN demo_patterns "|" demo_pattern_arg)   # a ';' would split the argument
set(embedded_patterns ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedPatterns.cpp)
add_custom_command(OUTPUT ${embedded_patterns}
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${embedded_patterns} -DFILES=${demo_pattern_arg}
            -P ${PROJECT_SOURCE_DIR}/cmake/EmbedPatterns.cmake
    DEPENDS ${demo_patterns} ${PROJECT_SOURCE_DIR}/cmake/EmbedPatterns.cmake
    COMMENT "Embedding the demo patterns"
    VERBATIM)
# Tens of megabytes of numbers: nothing for clang-tidy to find, and slow for it to read.
set_source_files_properties(${embedded_patterns} PROPERTIES SKIP_LINTING ON)

add_library(wxLife_lib STATIC
    core/BandedStepper.cpp core/Demo.cpp core/Format.cpp core/Grid.cpp core/Gzip.cpp core/HashLife.cpp
    core/Pacer.cpp core/ParallelBands.cpp core/Pattern.cpp core/PatternSetup.cpp core/ReferenceStepper.cpp
    core/Rule.cpp core/Speed.cpp core/StepKernel.cpp core/Stepper.cpp core/World.cpp core/WorldLimits.cpp
    ${embedded_patterns}
    render/PixelBuffer.cpp render/Rasterizer.cpp render/Thumbnail.cpp render/Viewport.cpp)
add_library(wxLife::lib ALIAS wxLife_lib)
target_include_directories(wxLife_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wxLife_lib PUBLIC Threads::Threads)
wxLife_configure_target(wxLife_lib)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The stepping loop, HashLife and the rasterizer are optimised in every configuration, so Debug builds
    # stay interactive: unoptimised, a maximised 4K canvas takes up to 90 ms per frame. Not with MSVC, whose
    # Debug runtime checks (/RTC1) cannot be combined with /O2.
    set_source_files_properties(core/StepKernel.cpp core/HashLife.cpp render/Rasterizer.cpp
                                PROPERTIES COMPILE_OPTIONS -O3)
endif()
if(WXLIFE_KERNEL_CLONES)
    set_source_files_properties(core/StepKernel.cpp PROPERTIES COMPILE_DEFINITIONS WXLIFE_KERNEL_CLONES)
endif()

if(NOT WXLIFE_BUILD_UI)
    return()
endif()

add_library(wxLife_ui STATIC
    app/LifeApp.cpp
    ui/CommandIds.cpp ui/ControlPanel.cpp ui/DemoDialog.cpp ui/MainFrame.cpp ui/MenuBar.cpp
    ui/SimulationRunner.cpp ui/WorldCanvas.cpp ui/WorldSizeDialog.cpp)
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
