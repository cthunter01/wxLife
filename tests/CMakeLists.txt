set(test_sources
    core/AntTest.cpp core/FormatTest.cpp core/GridTest.cpp core/LineTest.cpp core/PacerTest.cpp
    core/ParallelBandsTest.cpp core/RandomTest.cpp core/RuleTest.cpp core/SpeedTest.cpp
    core/StepperTest.cpp core/WorldLimitsTest.cpp core/WorldTest.cpp
    render/RasterizerTest.cpp render/ViewportTest.cpp)
if(WXLIFE_BUILD_UI)
    list(APPEND test_sources ui/GuiSmokeTest.cpp)
endif()

add_executable(wxLife_tests ${test_sources})
target_include_directories(wxLife_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})   # support/AsciiGrid.h
target_link_libraries(wxLife_tests PRIVATE wxLife::lib GTest::gtest_main)
if(WXLIFE_BUILD_UI)
    target_link_libraries(wxLife_tests PRIVATE wxLife::ui)
endif()
wxLife_configure_target(wxLife_tests)

include(GoogleTest)
# PRE_TEST: list the tests when ctest runs, not at build time (the binary may be sanitized or cross-compiled).
gtest_discover_tests(wxLife_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

if(WXLIFE_BUILD_UI)
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
if(WXLIFE_BUILD_UI AND UNIX AND NOT APPLE)
    add_test(NAME static_link
             COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:wxLife>
                     -P ${PROJECT_SOURCE_DIR}/cmake/CheckStaticLink.cmake)
endif()
