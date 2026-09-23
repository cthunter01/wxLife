set(test_sources
    core/AntTests.cpp core/DemoTests.cpp core/FormatTests.cpp core/GridTests.cpp core/LineTests.cpp
    core/PacerTests.cpp core/ParallelBandsTests.cpp core/PatternSetupTests.cpp core/PatternTests.cpp
    core/RandomTests.cpp core/RuleTests.cpp core/SpeedTests.cpp core/StepperTests.cpp
    core/WorldLimitsTests.cpp core/WorldTests.cpp
    render/RasterizerTests.cpp render/ThumbnailTests.cpp render/ViewportTests.cpp)
# The GUI smoke tests drive wxGTK: they type through GTK's own text entry and check how GTK behaves. So
# they are built only where wx uses GTK, which is Linux (wxBUILD_TOOLKIT is wxWidgets' cache variable).
set(gui_tests OFF)
if(WXLIFE_BUILD_UI AND wxBUILD_TOOLKIT MATCHES "^gtk")
    set(gui_tests ON)
    list(APPEND test_sources ui/GuiSmokeTests.cpp)
endif()

add_executable(wxLife_tests ${test_sources})
target_include_directories(wxLife_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})   # support/AsciiGrid.h
target_link_libraries(wxLife_tests PRIVATE wxLife::lib GTest::gtest_main)
if(gui_tests)
    target_link_libraries(wxLife_tests PRIVATE wxLife::ui)
endif()
wxLife_configure_target(wxLife_tests)

include(GoogleTest)
# PRE_TEST: list the tests when ctest runs, not at build time (the binary may be sanitized or cross-compiled).
gtest_discover_tests(wxLife_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

if(gui_tests)
    # The GUI smoke tests (GuiSmokeTest.*) each open a window, so they run one at a time, and they carry the
    # label 'gui' (ctest -LE gui leaves them out). GTK and fontconfig leak at exit by design, so under
    # AddressSanitizer they run without leak detection. The discovered names are only known when ctest runs,
    # so this script, included after the discovery, sets the properties.
    set(gui_properties "LABELS gui RUN_SERIAL TRUE")
    if("address" IN_LIST WXLIFE_SANITIZERS)
        string(APPEND gui_properties
               " ENVIRONMENT_MODIFICATION \"ASAN_OPTIONS=string_append::detect_leaks=0\"")
    endif()
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gui_test_properties.cmake @ONLY CONTENT [[
foreach(test IN LISTS wxLife_tests_TESTS)
    if(test MATCHES "^GuiSmokeTest\\.")
        set_tests_properties("${test}" PROPERTIES @gui_properties@)
    endif()
endforeach()
]])
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
                 ${CMAKE_CURRENT_BINARY_DIR}/gui_test_properties.cmake)
endif()

# Checks outside GoogleTest.
add_test(NAME layering
         COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                 -P ${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake)
if(WXLIFE_BUILD_UI)
    add_test(NAME static_link
             COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:wxLife>
                     -P ${PROJECT_SOURCE_DIR}/cmake/CheckStaticLink.cmake)
endif()
