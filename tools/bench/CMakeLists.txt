# Headless stepping benchmark; links only the simulation, never wx.
add_executable(wxLife_bench StepBench.cpp)
target_link_libraries(wxLife_bench PRIVATE wxLife::lib)
wxLife_configure_target(wxLife_bench)
