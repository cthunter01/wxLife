# wxLife

Conway's Game of Life and Langton's ant with a wxWidgets 3.2 interface. C++23 project built with CMake
presets + Ninja, tested with GoogleTest. Cross-platform: Linux (GCC, Clang; wxGTK 3), macOS (Apple Clang;
wxOSX/Cocoa) and Windows (MSVC; wxMSW). wxWidgets is downloaded and built statically on the first
configure of each preset (about half a minute on Linux).

## Commands
- Build and test (Clang Debug): `cmake --workflow --preset dev`
- Rebuild only: `cmake --build --preset clang-debug`
- Test only: `ctest --preset clang-debug`
- One test: `ctest --preset clang-debug -R 'WorldTest\.'` or
  `build/clang-debug/bin/wxLife_tests --gtest_filter='WorldTest.*'`
- Before finishing a change, also run: `cmake --workflow --preset tidy` (clang-tidy, warnings are errors) and
  `cmake --workflow --preset asan` (AddressSanitizer + UBSan)
- On Windows the presets are `msvc-debug` (workflow `dev-msvc`), `msvc-release` and `ci-msvc`, and cmake must run
  in a Developer PowerShell for VS. `tidy`, `asan`, `tsan` and `coverage` exist on Linux and macOS only
- Formatting is automatic: a Claude Code hook (`.claude/hooks/format-cpp.sh`) runs clang-format on every C/C++
  file right after you edit it. The pre-commit hook and CI also reject unformatted files
- The GUI smoke tests (`GuiSmokeTest.*`, label `gui`) are built only with wxGTK (Linux). They open real
  windows whenever a display is set. Run them off-screen on GTK's Broadway backend: start `broadwayd :5` in
  the background, then prefix ctest or the workflow with `GDK_BACKEND=broadway BROADWAY_DISPLAY=:5`.
  `ctest -LE gui` skips them

Other presets: `clang-release`, `gcc-debug`, `gcc-release`, `tsan` (no UI), `headless` (no UI, no wxWidgets
download), `coverage`, `ci-gcc`, `ci-clang`.
Each builds into `build/<preset>/`; never edit anything under `build/`. A preset is only available on the
platforms it supports (`gcc-*` and `headless`: Linux; `clang-*`: Linux and macOS; `msvc-*`: Windows);
`cmake --list-presets` shows this machine's.

## Layout
- `include/wxLife/<layer>/`: headers; `src/<layer>/`: sources. Layers, lowest first: `core` (simulation),
  `render` (camera and pixels), `ui` (wx windows), `app` (the wxApp)
- `src/`: `wxLife_lib` (`core` + `render`, never wx), `wxLife_ui` (`ui` + `app`, links wx) and the `wxLife`
  executable (`Main.cpp`; a GUI-subsystem program on Windows, `wxLife.app` on macOS)
- `tests/<layer>/`: GoogleTest files, all in `wxLife_tests`; `tests/support/`: helpers
- `tools/bench/`: `wxLife_bench`, the headless stepping benchmark
- `patterns/`: the demo patterns' files, RLE and gzip-compressed macrocell (`.mc.gz`), embedded into
  `wxLife_lib` by `cmake/EmbedPatterns.cmake`. Each is listed in `demo_patterns` (`src/CMakeLists.txt`) and
  used by a demo in `src/core/Demo.cpp`; `DemoTest` checks both. `patterns/README.md` says how to add one
- `cmake/ProjectOptions.cmake`: `wxLife_configure_target()` (warnings, sanitizers, coverage, tidy)
- `cmake/Dependencies.cmake`: third-party libraries via FetchContent (GoogleTest, static wxWidgets)
- `docs/architecture.md`: the design, and where to extend it

## Conventions
- Headers are `.h` (never `.hpp`) and use `#pragma once`
- Source file names (.c, .cpp, .h) are UpperCamelCase: `WorldCanvas.h`, `StepKernel.cpp`, `Main.cpp`.
  Directories stay lowercase (`include/wxLife/core/`)
- A class's header and implementation files are named exactly after the class, including capitalization:
  `class World` lives in `include/wxLife/core/World.h` and `src/core/World.cpp`. Its tests are in
  `tests/core/WorldTests.cpp`, which includes the header under test first. Tests of anything else are
  `<Name>Tests.cpp` too (`GuiSmokeTests.cpp`)
- Identifier names are checked by clang-tidy (`readability-identifier-naming` in `.clang-tidy`): types
  `CamelCase`, enumerators `UPPER_CASE` (`Topology::TORUS`, and the wx command ids `ID_RUN_PAUSE`), functions
  and variables `camelBack`, private and protected members `m_name`, static members and statics `s_name`,
  constants `kName`. Avoid enumerators that are macros on some platform, such as `ERROR` (`<windows.h>`)
- Code lives in `namespace wxLife::<layer>`; project includes use quotes: `#include "wxLife/core/World.h"`
- No layer includes a layer above it, and `core` and `render` never include wx. The `layering` test checks this
- Every new target must call `wxLife_configure_target(<target>)`
- New source files go into the relevant `CMakeLists.txt`; new tests go into `tests/CMakeLists.txt`
- Warnings are part of the build: code must compile cleanly with `-Werror` under GCC and Clang and with `/WX`
  under MSVC
- Code must build and pass its tests on Linux, macOS and Windows (CI runs all three). Use the standard library
  (`<filesystem>`, `<thread>`, `<chrono>`) and wx over POSIX or Win32 APIs; when an OS API is unavoidable, keep
  it in one source file behind an `#ifdef _WIN32` / `__APPLE__` / `__linux__` split, with a branch for each
  platform (like `physicalMemoryBytes()` in `src/core/WorldLimits.cpp`)
- `.clang-tidy` files in `tests/`, `src/ui/` and `tests/ui/` adjust the root one for GoogleTest and wx
