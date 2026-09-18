# wxLife

Conway's Game of Life and Langton's ant with a wxWidgets 3.2 (GTK 3) interface. C++23 project built
with CMake presets + Ninja, tested with GoogleTest. Linux, GCC and Clang. wxWidgets is downloaded and
built statically on the first configure of each preset (about half a minute).

## Commands
- Build and test (Clang Debug): `cmake --workflow --preset dev`
- Rebuild only: `cmake --build --preset clang-debug`
- Test only: `ctest --preset clang-debug`
- One test: `ctest --preset clang-debug -R 'WorldTest\.'` or
  `build/clang-debug/bin/wxLife_tests --gtest_filter='WorldTest.*'`
- Before finishing a change, also run: `cmake --workflow --preset tidy` (clang-tidy, warnings are errors) and
  `cmake --workflow --preset asan` (AddressSanitizer + UBSan)
- Formatting is automatic: a Claude Code hook (`.claude/hooks/format-cpp.sh`) runs clang-format on every C/C++
  file right after you edit it. The pre-commit hook and CI also reject unformatted files
- The GUI smoke tests (`GuiSmokeTest.*`, label `gui`) open real windows whenever a display is set. Run them
  off-screen on GTK's Broadway backend: start `broadwayd :5` in the background, then prefix ctest or the
  workflow with `GDK_BACKEND=broadway BROADWAY_DISPLAY=:5`. `ctest -LE gui` skips them

Other presets: `clang-release`, `gcc-debug`, `gcc-release`, `tsan` (no UI), `headless` (no UI, no wxWidgets
download), `coverage`, `ci-gcc`, `ci-clang`.
Each builds into `build/<preset>/`; never edit anything under `build/`.

## Layout
- `include/wxLife/<layer>/`: headers; `src/<layer>/`: sources. Layers, lowest first: `core` (simulation),
  `render` (camera and pixels), `ui` (wx windows), `app` (the wxApp)
- `src/`: `wxLife_lib` (`core` + `render`, never wx), `wxLife_ui` (`ui` + `app`, links wx) and the `wxLife`
  executable (`Main.cpp`)
- `tests/<layer>/`: GoogleTest files, named `*Test.cpp`, all in `wxLife_tests`; `tests/support/`: helpers
- `tools/bench/`: `wxLife_bench`, the headless stepping benchmark
- `cmake/ProjectOptions.cmake`: `wxLife_configure_target()` (warnings, sanitizers, coverage, tidy)
- `cmake/Dependencies.cmake`: third-party libraries via FetchContent (GoogleTest, static wxWidgets)
- `docs/architecture.md`: the design, and where to extend it

## Conventions
- Headers are `.h` (never `.hpp`) and use `#pragma once`
- Source file names (.c, .cpp, .h) are UpperCamelCase: `WorldCanvas.h`, `StepKernel.cpp`, `WorldTest.cpp`,
  `Main.cpp`. Directories stay lowercase (`include/wxLife/core/`)
- Code lives in `namespace wxLife::<layer>`; project includes use quotes: `#include "wxLife/core/World.h"`
- No layer includes a layer above it, and `core` and `render` never include wx. The `layering` test checks this
- Every new target must call `wxLife_configure_target(<target>)`
- New source files go into the relevant `CMakeLists.txt`; new tests go into `tests/CMakeLists.txt`
- Warnings are part of the build: code must compile cleanly with `-Werror` under both GCC and Clang
- `.clang-tidy` files in `tests/`, `src/ui/` and `tests/ui/` adjust the root one for GoogleTest and wx
