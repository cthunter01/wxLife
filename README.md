# wxLife

wxLife is Conway's Game of Life for Linux, macOS and Windows, written in C++23 with a wxWidgets 3.2
interface (GTK 3 on Linux, Cocoa on macOS, Win32 on Windows). It runs Conway's Life and any other
two-state B/S rule on worlds with sides of up to 100,000 cells, as far as a memory budget allows, with
wrapping or dead edges, or on an unbounded plane with HashLife, and it also runs Langton's ant on the
fixed-size worlds. The code is split into small layers so it is easy to study and extend. A 1000 × 1000
world runs smoothly at every zoom level, and a 10000 × 10000 world stays usable.
[docs/architecture.md](docs/architecture.md) explains the design.

## Features

- Any B/S rule (`B3/S23`, `B36/S23`, `B/S`, …) plus ten presets: Conway's Life, HighLife, Seeds,
  Day & Night, Life without Death, Maze, 2x2, Replicator, Diamoeba and Morley.
- **Demo patterns**: File → Demo Patterns… (or the panel's Demos… button) lists 43 famous patterns in
  eight groups: spaceships, guns, puffers and rakes, breeders, methuselahs, oscillators, computation
  (the Primer prime sieve, the twin prime and (p, p+8) prime calculators, and Paul Rendell's Turing
  machine and universal Turing machine) and Langton's ant setups. The dialog shows who found each one,
  a preview and what to watch for. Loading one sets up the world it runs best in: size, edges, speed
  and the part of the world to show.
- **Pattern files**: File → Open Pattern… reads RLE (`.rle`) and plaintext (`.cells`) files, such as
  the ones on LifeWiki, into a world with room around the pattern.
- **Langton's ant** as a second automaton, chosen in Simulation → Automaton or in the side panel. Each
  ant turns right on a dead cell and left on a live one, flips the cell and steps forward; a generation
  is one move for every ant. Up to 64 ants share a world and move in order, so each one sees what the
  ones before it have just left. An ant always wraps at the edges, whatever the world's own edges do, so
  Wrap Edges, the rule and the engine are greyed out while it runs. Ants are placed with Edit → Reset
  Ants, with the panel's Ants box, or one at a time with Ctrl+left click.
- **Unbounded worlds**: World → Size… offers an unbounded plane besides a fixed size. It runs Life
  rules with Bill Gosper's HashLife algorithm, which remembers how every part of the pattern evolves, so
  regular patterns can jump 2^k generations in one step: the step size goes from 1 to 2^40 generations
  (the panel's Step box, F7 and F8, `{` and `}`). Steps run in the background, so even a step of
  minutes leaves the window responsive, and the status bar says "Computing…" meanwhile. Positions are
  64-bit, and a pattern has 2^59 cells in every direction before it meets the edge of the universe.
- Worlds from 1 × 1 cell to 100,000 cells per side. A memory budget (a quarter of the RAM, at most
  16 GiB) limits the total, so a square world has at most about 92,000 cells per side. Edges either wrap
  (a torus) or are dead.
- Two engines:
  - **Banded** (the default) splits the rows of a large world into bands that run on several threads.
    On x86-64 Linux its inner loop is built for AVX2 and for baseline x86-64, and the right version is
    chosen at load time.
  - **Reference** counts neighbours the plain way. It is the specification that the tests compare the
    Banded engine against.
- Speed from 1 to 1000 generations per second, or Max. The status bar shows the target and, once it has
  been measured, the rate actually achieved.
- Zoom from 100 screen pixels per cell down to 1 px, and below: at 1/2, 1/4, 1/8 px… each pixel shows a
  block of cells and lights up if any of them is alive, so even a lone glider stays visible. Zooming is
  anchored at the mouse pointer, and zooming out stops once the whole world is in view (for an
  unbounded plane, the whole universe). Grid lines appear from 5 px, with a stronger line every 10
  cells.
- Drawing with the mouse, with no gaps even when the mouse moves fast. Panning, scrollbars and keyboard
  shortcuts.
- One cell can be one physical pixel on HiDPI screens. The colours follow the desktop's light or dark
  theme, also when it changes while the app runs.
- wxWidgets is linked statically, so the binary needs no wxWidgets libraries. On Linux it needs, besides
  GTK 3, only libSM, libICE and PCRE2 (`libpcre2-32`) from the system. On macOS it needs only the system's
  frameworks, and on Windows only the Microsoft Visual C++ runtime that every MSVC program needs.

## Requirements

- Linux, macOS 13.3 or newer, or Windows 10 or newer (x64). CI builds wxLife and runs its tests on all
  three; the app is developed and checked by hand on Linux.
- CMake 3.28+ and Ninja.
- A C++23 compiler with `<print>`:
  - Linux: GCC 14+ or Clang 18+. Tested with GCC 16 and Clang 22.
  - macOS: Xcode 16.3+ or its Command Line Tools (Apple Clang 17+).
  - Windows: Visual Studio 2022 17.7+ (MSVC) with the "Desktop development with C++" workload.
- Linux only: GTK 3 development files and pkg-config:
  - Debian/Ubuntu: `libgtk-3-dev`
  - Fedora: `gtk3-devel`
  - Arch: `gtk3`

  The zlib, libpng, expat and PCRE2 development headers are needed too; GTK's development packages
  normally pull them in. Without them wxWidgets falls back to its bundled copies, and its bundled zlib
  does not compile with GCC 14 or newer. macOS and Windows need nothing else: wxWidgets uses the system's
  own toolkit there and builds its bundled libraries.
- Network access on the first configure. CMake downloads wxWidgets 3.2.11 (27 MB, checked by SHA-256),
  then builds it from source as static libraries. That build takes about half a minute on 20 cores
  (longer with MSVC) and is done once per build directory. GoogleTest is used from the system when installed, otherwise
  downloaded too.
- Optional: clang-tidy, clang-format, llvm-cov/llvm-profdata (coverage), Doxygen and Graphviz (docs),
  ccache (used automatically unless you set a compiler launcher yourself). On macOS, clang-tidy comes from
  Homebrew (`brew install llvm`); coverage uses Xcode's llvm-cov.

## Build, run and test

Linux and macOS:
```sh
cmake --workflow --preset dev          # configure + build + test, Clang Debug
./build/clang-debug/bin/wxLife         # Linux
open build/clang-debug/bin/wxLife.app  # macOS
```

Windows, from a **Developer PowerShell for VS** (Ninja needs MSVC's environment; VS Code's CMake Tools and
Visual Studio set it up themselves):
```powershell
cmake --workflow --preset dev-msvc     # configure + build + test, MSVC Debug
.\build\msvc-debug\bin\wxLife.exe
```

| Preset | Platforms | What it is |
| --- | --- | --- |
| `clang-debug`, `clang-release` | Linux, macOS | Everyday builds (Apple Clang on macOS) |
| `gcc-debug`, `gcc-release` | Linux | Everyday builds |
| `msvc-debug`, `msvc-release` | Windows | Everyday builds |
| `asan` | Linux, macOS | Clang Debug with AddressSanitizer + UndefinedBehaviorSanitizer; undefined behaviour stops the program |
| `tsan` | Linux, macOS | Clang RelWithDebInfo with ThreadSanitizer. No UI: GTK is not instrumented |
| `headless` | Linux | GCC Debug without the UI and without the wxWidgets download, for fast work on `core` and `render`. Builds only the baseline stepping loop (no AVX2 version), so its tests cover that one |
| `tidy` | Linux, macOS | Clang Debug running clang-tidy on every file; findings are errors |
| `coverage` | Linux, macOS | `cmake --workflow --preset coverage` writes `build/coverage/coverage/html/index.html` |
| `ci-gcc`, `ci-clang`, `ci-msvc` | as their compiler | Release builds with warnings as errors, as run in CI |

A preset exists only on the platforms it supports; `cmake --list-presets` shows the ones for this machine.
Each workflow preset (`dev`, `dev-msvc`, `ci-gcc`, `ci-clang`, `ci-msvc`, `asan`, `tsan`, `headless`,
`tidy`, `coverage`) configures, builds and tests in one command. Separate steps: `cmake --preset <p>`,
`cmake --build --preset <p>`, `ctest --preset <p>`. Each preset builds in `build/<preset>/`, with the
programs in `build/<preset>/bin/`.

CI (GitHub Actions) builds and tests on all three platforms: Linux (`ci-gcc`, `ci-clang`, `asan`,
`tidy`), macOS (`ci-clang`) and Windows (`ci-msvc`).

Debug builds check the standard library: `_GLIBCXX_ASSERTIONS` for libstdc++ (Linux), the extensive
hardening mode for libc++ (macOS), and MSVC's Debug library checks itself. With GCC and Clang, the
stepping loop and the rasterizer are compiled with `-O3` in every build type, so Debug builds stay
responsive.

**What gets built:**

| Target | What it is |
|---|---|
| `wxLife` | The app (`wxLife.app` on macOS) |
| `wxLife_lib` | The simulation and its rendering (`core`, `render`); never uses wx |
| `wxLife_ui` | The wxWidgets interface (`ui`, `app`) |
| `wxLife_tests` | All GoogleTest suites: unit tests for `core` and `render`, and on Linux the GUI smoke tests |
| `wxLife_bench` | Headless stepping benchmark |
| `docs` | API documentation (not built by default) |

**CTest runs:**
- the unit tests. They need no display;
- on Linux, the GUI smoke tests (`GuiSmokeTest.*`, label `gui`), which drive the real main window. They
  are built only with wxGTK, because they type through GTK's own text entry and check GTK's behaviour.
  Each one opens a window, so they run one at a time. They are skipped when no display is set (`DISPLAY`,
  `WAYLAND_DISPLAY` and `BROADWAY_DISPLAY` all empty, and `GDK_BACKEND` not naming broadway), and they
  fail when a display is set but GTK cannot open any. `ctest --preset clang-debug -LE gui` leaves them
  out. To keep their windows off your desktop, run them on GTK's Broadway backend:
  `broadwayd :5 &` and then `GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 ctest --preset clang-debug`;
- `layering`, which fails if a layer includes code from a layer above it (see below);
- `static_link`, which fails if `wxLife` loads a shared wxWidgets library (checked with `ldd` on Linux,
  `otool -L` on macOS and `dumpbin /dependents` on Windows).

**Benchmark:**

```sh
./build/clang-release/bin/wxLife_bench --size 10000x10000 --generations 20
```

The benchmark prints the number of bands, milliseconds per generation and nanoseconds per cell. It also
takes `--density`, `--threads`, `--bands` (an exact band count, for comparing splits),
`--engine banded|reference` (case does not matter), `--rule` and `--bounded`; `--help` lists them. On
the development machine (20 threads), a 10000 × 10000 world takes about 5.5 ms per generation.

**Documentation:** `cmake --build --preset clang-debug --target docs`, then open
`build/clang-debug/docs/html/index.html`. This README is its main page.

**CMake options.** Pass these with `-D`, in addition to a preset if you like:

| Option | Default | Meaning |
|---|---|---|
| `WXLIFE_BUILD_UI` | `ON` | Build the app; `OFF` also skips the wxWidgets download |
| `WXLIFE_BUILD_TESTS` | `ON` (top level) | Build the tests (the GUI tests only with the UI) |
| `WXLIFE_BUILD_BENCH` | `ON` (top level) | Build `wxLife_bench` |
| `WXLIFE_BUILD_DOCS` | `ON` (top level) | Add the `docs` target when Doxygen is found |
| `WXLIFE_KERNEL_CLONES` | `ON` | Build the AVX2 and baseline versions of the stepping loop (x86-64 with glibc only) |
| `WXLIFE_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler and clang-tidy warnings in wxLife's own code as errors |
| `WXLIFE_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy while compiling |
| `WXLIFE_ENABLE_COVERAGE` | `OFF` | Clang source-based coverage and a `coverage` target |
| `WXLIFE_ENABLE_IPO` | `OFF` | Link-time optimisation; CMake warns if the toolchain cannot do it |
| `WXLIFE_STATIC_LIBSTDCXX` | `OFF` | Also link libstdc++ and libgcc statically into the app |
| `WXLIFE_SANITIZERS` | empty | `address;undefined` or `thread`. With `undefined`, the first error stops the program |

`cmake --install build/clang-release --prefix ~/.local` installs `bin/wxLife`.

wxLife also works as part of a larger CMake project (`add_subdirectory()` or FetchContent). It then keeps
the parent's build type and `CMAKE_MODULE_PATH`, and it builds no tests, benchmark or docs target unless
asked to. Its install rule still installs `wxLife`.

## Controls

Single-key shortcuts only work while the world has keyboard focus. The world has it at start-up, and it
gets it back when you click the world or use a panel button or check box (except Apply), with the mouse
or the keyboard, so Space then runs or pauses instead of pressing the button again.

The menu shortcuts (Ctrl or a function key) work everywhere, including while you type in the rule box,
with one exception: a focused text or number box keeps its own editing keys, such as Ctrl+Home and
Ctrl+Delete (in a number box, Ctrl+Home sets the smallest value).

**Help → Keyboard and Mouse…** (F1) shows the same lists inside the app.

### Mouse

| Input | Action |
|---|---|
| Left press and drag | Draw. Starting on a live cell erases instead. |
| Right press and drag | Erase |
| Middle drag, or Shift + left drag | Pan |
| Ctrl + left click | Add an ant, or take away the one already there (Langton's ant only) |
| Wheel | Scroll up and down |
| Shift + wheel, or a horizontal wheel or touchpad swipe | Scroll left and right |
| Ctrl + wheel | Zoom one step per notch, keeping the cell under the pointer in place |
| Hover | The status bar shows the cell's coordinates (below 1 px, the first cell of the pixel's block) |

Below 1 px per cell a pixel shows many cells, so no drag draws or erases there, and no click places an
ant: every drag pans instead. A stroke in progress waits while the view is zoomed out that far.

Each wheel notch scrolls 3 cells, but at least 48 pixels. Pressing outside the world starts no stroke.
Only the button that started a drag ends it. If the view moves during a stroke (a key, the wheel or a
scrollbar), the stroke goes on from the cell now under the pointer, with no line across the jump.

### Keys while the world has focus

| Key | Action |
|---|---|
| Space | Run or pause |
| N | Step one generation (on an unbounded plane, one step of the current size) |
| `]` / `[` | Faster / slower |
| `}` / `{` | Larger / smaller step, twice or half the generations per step (unbounded worlds) |
| `+` or `=` / `-` (also on the keypad) | Zoom in / out |
| F | Fit the world into the view |
| C or Home | Center the world |
| G | Grid lines on or off |
| W | Wrap edges on or off |
| Arrow keys | Pan by 10% of the view (with Shift: 90%) |
| Page Up / Page Down | Pan by 90% of the view's height |
| Esc | End the current stroke (the cells already drawn stay) |

`]`, `[`, `}`, `{`, `+`, `=` and `-` are matched by the character they type, so they work on any
keyboard layout, AltGr combinations included. An input method (for example for Chinese or Japanese) may
take these keys first; the keypad `+` and `-` and the menu shortcuts still work. Clear, Randomize and
Resize have no single-key shortcut, so they cannot be triggered by accident.

### Menu shortcuts

| Shortcut | Menu → item |
|---|---|
| F5 | Simulation → Run / Pause |
| F6 | Simulation → Step |
| Ctrl+] / Ctrl+[ | Simulation → Faster / Slower |
| Ctrl+M | Simulation → Max Speed |
| F8 / F7 | Simulation → Larger Step / Smaller Step (unbounded worlds) |
| none | Simulation → Automaton → Life / Langton's Ant |
| none | Simulation → Engine → Banded / Reference |
| Ctrl+Delete | Edit → Clear |
| Ctrl+R | Edit → Randomize (at the panel's density) |
| none | Edit → Reset Ants (back to their starting spots) |
| Ctrl+L | Edit → Edit Rule… (moves the focus to the rule box) |
| Ctrl+N | World → Size… |
| Ctrl+T | World → Wrap Edges |
| Ctrl+= / Ctrl+- | View → Zoom In / Zoom Out (keeps the centre of the view in place) |
| Ctrl+0 | View → Fit World |
| Ctrl+Home | View → Center World |
| Ctrl+G | View → Grid Lines |
| Ctrl+O | File → Open Pattern… |
| Ctrl+D | File → Demo Patterns… |
| F1 | Help → Keyboard and Mouse… |
| Ctrl+Q | File → Quit |

The panel on the left has the same actions, plus:
- the automaton to run, and how many ants it gets (1–64) with a Reset button beside it;
- the random-fill density (1–100%);
- a Demos… button next to Resize…, which opens the demo patterns;
- an exact speed box and an exact cell-size box;
- on an unbounded plane, the step size as a power of two, with the number of generations it means;
- a rule box: type a rule and press Enter, or click Apply.

Whichever automaton is running greys out what only the other one uses, so a control that would do
nothing is visibly dead: Langton's ant disables the Rule group, Wrap Edges and the Engine submenu, and
Life disables the ant count and Reset Ants. An unbounded plane likewise disables the automaton choice,
Wrap Edges and the Engine submenu, and only a plane enables the step size.

A rule the app cannot read shows an error below the box, and the current rule stays in effect. The mouse
wheel changes a slider, a number box or the preset list only while that control has the focus. Over an
unfocused one the wheel does nothing, and the panel does not scroll either.

## Layers

| Layer | Headers and sources | CMake target | Contents | Uses |
|---|---|---|---|---|
| core | `include/wxLife/core`, `src/core` | `wxLife_lib` | Simulation, pattern files and demos, size limits, speed and pacing | Standard library and threads |
| render | `include/wxLife/render`, `src/render` | `wxLife_lib` | Pixel types, camera, pixel rasterizer and thumbnails | core |
| ui | `include/wxLife/ui`, `src/ui` | `wxLife_ui` | Windows, input, theme colours and the simulation timer | render, core, wxWidgets |
| app | `include/wxLife/app`, `src/app` | `wxLife_ui` | `LifeApp`, which owns the `World` | ui, wxWidgets |

`src/Main.cpp` is the `wxLife` executable. `wxLife_bench` links only `wxLife_lib`. Only `ui` and `app`
include wx headers; the `layering` test checks the include lines of `core`, `render` and `ui`. For the
full picture (who owns what, how a click becomes a redraw, and where to extend the code), read
[docs/architecture.md](docs/architecture.md).

## Configuration notes

wxLife has no settings file. The start-up values are in `include/wxLife/ui/Defaults.h`: a 512 × 512 world
with wrapping edges and Conway's rule, 25% random fill, one ant, 30 generations per second, and paused.

- **World size.** Each side can be 1 to 100,000 cells.
  - A world needs 2 × (width + 2) × (height + 2) bytes. For example, 1000² needs 1.9 MiB, 10000² needs
    191 MiB and 20000² needs 763 MiB.
  - The memory budget is a quarter of the RAM, but at least 256 MiB and at most 16 GiB (2 GiB if the RAM
    size cannot be read).
  - The size dialog checks the text of both boxes as you type. It shows the memory cost or what is wrong
    (an empty box, a number out of range, a size over the budget), and neither OK nor Enter accepts the
    size until it is valid.
  - During a resize the old and the new world exist at the same time.
  - "Keep the current pattern" keeps the pattern centred.
  - **Unbounded** in the same dialog turns the world into a plane. Its memory grows with the pattern,
    up to the same budget; the panel shows what it uses. Keeping the pattern puts the centre of the
    old world at (0, 0), and going back to a fixed size puts (0, 0) at the centre of the new world.
    A plane runs only Life, and only rules without B0 (a birth on 0 neighbours would fill the whole
    plane at once): the dialog refuses Unbounded while Langton's ant runs or the rule has B0, and says
    why, and the rule box refuses a B0 rule while a plane runs.
  - On a plane, Randomize fills what the view shows (at most its middle 4096 × 4096 cells), Fit shows
    the whole pattern, and the scrollbars do nothing; pan with the mouse or the keys. From 1 px up,
    the view can go 2^55 pixels from the centre; zoomed out, it reaches the edge of the universe.
- **Demo patterns.** Loading a demo replaces the world, pauses at generation 0 and sets the world size,
  edges, rule, speed and automaton the demo was tuned for. Every setting was chosen by running the
  demo: gliders from the guns vanish cleanly at the dead edges, and the methuselahs evolve exactly as
  on an unbounded plane.
  - The prime calculators send streams of spaceships up and to the right, which an unbounded plane
    would swallow. Here they crash into the edges, and the wreckage comes back after 8,000 to 14,000
    generations; each description says how far its calculator is right until then.
  - The universal Turing machine's world needs about 330 MB. A demo that does not fit the memory
    budget is listed but cannot be loaded, and the dialog says why.
  - Patterns far larger than a fixed-size world can be, such as the Caterpillar spaceship (4,195 ×
    330,721 cells), are published as macrocell files, which wxLife does not read yet, so they are not
    included. [patterns/README.md](patterns/README.md) lists the sources and how to add a demo.
- **Pattern files.** File → Open Pattern… puts the pattern in the middle of a new world with half its
  size, but at least 50 cells, of room on each side, shrunk to fit the memory budget if needed. The
  rule is the one the file names (RLE `rule =` or `#r`); a file without one keeps the current rule, and
  the edges and the speed stay as they are. On an unbounded plane the pattern goes onto the cleared
  plane, centred on (0, 0); a file whose rule has B0 is refused there with a message. Macrocell
  (`.mc`), Life 1.05/1.06 and multi-state files are refused with a message, and so is a pattern wider
  or taller than 100,000 cells.
- **Engine.** The Reference engine can be chosen only for worlds of up to 1,000,000 cells. Resizing to
  a larger world switches back to Banded. Only Life uses an engine at all.
- **Langton's ant.** A world carries 0 to 64 ants. Switching to the ant seeds one in the middle;
  more of them are spread evenly along the middle row, all facing north. Clear, Randomize and a resize
  that keeps nothing put them back on those spots, because generation 0 means the ants have not moved
  yet; a resize that keeps the pattern moves them with it and pulls a cropped one back inside. The
  ants survive a switch to Life and back, and they always wrap at the edges whatever the topology says.
- **Speed.**
  - The target is 1 to 1000 generations per second, or Max.
  - The slider is logarithmic, and the box next to it takes any exact value.
  - Faster and Slower step through 1, 2, 5, 10, 15, 20, 30, 60, 120, 250, 500 and 1000; one step past
    1000 is Max.
  - Each timer tick stops stepping once 10 ms have passed, so a tick takes about 10 ms plus at most one
    generation. Once a world is too large for the target rate, it runs as fast as the machine allows.
  - After a stall, the simulation drops the backlog instead of catching up in a burst.
  - On an unbounded plane the speed still counts generations per second, but they come 2^k at a time:
    at 30 gen/s with steps of 2^10, a step starts about every 34 seconds, and at Max each step starts
    as soon as the one before has finished. A step that runs short of memory is tried once more after
    the unused parts of the plane have been freed; if it still does not fit, or the pattern reaches the
    edge of the universe, the simulation stops and says why. A smaller step may then still fit.
  - The status bar shows the target (`30 gen/s` or `Max`), then the achieved rate once it has been
    measured (`30 gen/s (29.9)` or `Max (… gen/s)`). A measurement takes at least half a second and two
    generations.
- **Cell size.** The cell size is 1 to 100 *device* pixels, so at 1 px one cell is one physical pixel
  even on a HiDPI screen, or below 1 px, 1/2^k px: one pixel for a block of 2^k × 2^k cells.
  - wx reports the mouse position in whole logical pixels. At a display scale of 2, cells smaller than
    2 px can therefore be clicked only in every second row and column. Zoom in to edit single cells.
  - The zoom commands and the slider step through 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 32, 40, 50,
    64, 80 and 100, and below 1 px through 1/2, 1/4, 1/8 and so on. The box accepts any size from 1 to
    100 in between; below 1 px the panel shows the scale as text (`1/16 px`) in its place, as the
    status bar does.
  - Zooming out stops at the first scale that shows the whole world: a world that fits at 1 px does not
    go below it. An unbounded plane goes on until its whole universe, 2^62 cells across, is in view.
  - Below 1 px a pixel is alive if any cell of its block is, so dense areas look solid. Drawing a large
    grid there reads every visible cell, on several threads: a whole 20000² world takes about 7 ms per
    frame on the development machine.
  - Fit (also run after every resize) picks the largest scale that shows the whole world, below 1 px if
    need be. The world then stays fitted as the window changes size, until you zoom or scroll. Demos in
    worlds larger than the window therefore open zoomed out, showing the whole world.

## Troubleshooting

The notes about GTK, X11 and Wayland apply to Linux only.

- **Wayland oddities.** `GDK_BACKEND=x11 ./build/clang-debug/bin/wxLife` runs the app through XWayland.
  Use this to check whether a problem is specific to Wayland.
- **Blurry 1 px cells.** With fractional scaling (for example 125% on KDE), the compositor resamples the
  window. Use an integer scale factor, or run with `GDK_BACKEND=x11`. `GDK_SCALE=2` tests HiDPI
  behaviour on a normal screen.
- **Scrollbars.** wxLife sets `GTK_OVERLAY_SCROLLING=0` at startup, unless you set it yourself, because
  GTK overlay scrollbars make the reported canvas size wrong.
- **The GUI under AddressSanitizer.** GTK and fontconfig leak memory at exit by design. Run the GUI with
  `ASAN_OPTIONS=detect_leaks=0 ./build/asan/bin/wxLife`. `ctest --preset asan` keeps leak detection on
  for the unit tests and turns it off for the GUI tests.
- **GTK and Pango messages under Broadway.** On GTK's Broadway backend, wxWidgets' window-decoration
  code makes GTK and Pango print `CRITICAL` messages. They do not affect the tests.
- **Offline builds.** Download
  [wxWidgets-3.2.11.tar.bz2](https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.11/wxWidgets-3.2.11.tar.bz2)
  once, unpack it, and point CMake at the unpacked tree:
  ```sh
  cmake --preset clang-debug -DFETCHCONTENT_SOURCE_DIR_WXWIDGETS=/path/to/wxWidgets-3.2.11
  ```
  Offline tests also need GoogleTest installed on the system, or `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=…`.
  To skip wxWidgets entirely, use the `headless` preset.
- **"wxLife needs wx::core as a static library".** Configuring stops with this message when CMake was
  told to use an installed wxWidgets, for example with `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS`.
  wxLife links only the static wxWidgets it builds itself; use `FETCHCONTENT_SOURCE_DIR_WXWIDGETS` for a
  local copy of the sources.
- **Compiler warnings from wxWidgets.** GCC prints a few `-Wmaybe-uninitialized` warnings inside
  wxWidgets' own sources. They are harmless. `WXLIFE_WARNINGS_AS_ERRORS` applies only to wxLife's own
  targets.

## Manual smoke checklist

The GUI smoke tests send wx events straight to the windows, so they do not exercise GTK's own key and
mouse handling. Before a release, check these by hand, on X11 and on Wayland. On macOS and Windows, where
the GUI smoke tests do not run at all, the whole list is the only check of the interface:

- [ ] The 512² start-up world opens fully visible and centred, and stays fitted while the window is
      resized or maximised.
- [ ] Draw, erase and pan with the mouse. A fast drag leaves no gaps. Quick clicks on two different
      cells paint both cells (wxGTK reports the second click as a double click).
- [ ] Ctrl + wheel zooms at the pointer, all the way from 1 px to 100 px and back. Smooth-scrolling
      touchpads scroll and zoom without jumps.
- [ ] Resize to 10000² and Randomize: Fit zooms out below 1 px to show it all, the panel shows `1/16 px`
      (or similar) instead of the size box, and a left drag pans without drawing. Zooming out further
      does nothing; zooming in brings the size box back at 1 px.
- [ ] Dragging a scrollbar thumb scrolls the view. The scrollbar arrows and the page areas work too.
- [ ] Resize to 1 × 1, 10000² and 20000², keeping the pattern. The pattern stays centred.
- [ ] Max speed on a 10000² world: the window still repaints and responds to input.
- [ ] Enter a bad rule (for example `B9`): an error appears, and the old rule keeps running. Choosing a
      preset clears the error.
- [ ] Switch the engine to Reference and back. Reference is greyed out above 1,000,000 cells.
- [ ] Switch to Langton's ant, Clear, then Run at max speed: the ant is chaotic for about ten thousand
      generations and then builds a straight highway. The Rule group, Wrap Edges and the Engine submenu
      are greyed out while it runs.
- [ ] Ctrl+click the world in ant mode: an ant appears and the ant count follows; Ctrl+click it again
      and the ant goes away. Neither click draws a cell, and a plain left drag still draws.
- [ ] Click the rule box: the menu shortcuts (F5, Ctrl+R, Ctrl+M, …) still work, plain keys type text,
      and Ctrl+Delete deletes text there.
- [ ] Click Randomize, then press Space: the simulation runs. Ctrl+Home on the world centres the view.
- [ ] On another keyboard layout (for example German, where `]` is AltGr+9), `]`, `[`, `+` and `-` work.
- [ ] The wheel over an unfocused slider, number box or preset list changes nothing.
- [ ] Switch the desktop between light and dark while the app runs: the world and the error lines follow.
- [ ] Switch windows during a drag (capture loss), then keep using the mouse.
- [ ] Run with `GDK_BACKEND=x11` and with `GDK_SCALE=2`: at 1 px, one cell is one physical pixel.
- [ ] File → Demo Patterns…: the preview, the credit and the description follow the selection; a
      double click or Enter on a demo loads it, on a group opens or closes the group. With
      `GDK_SCALE=2` the preview is sharp.
- [ ] Load the Primer and run it at Max: lightweight spaceships leave the sieve to the left, 120
      generations apart for 2, 3, 5 and 7.
- [ ] File → Open Pattern…: open an `.rle` and a `.cells` file downloaded from LifeWiki, and a text file
      that is neither, which is refused with a message.
- [ ] World → Size…, Unbounded, keeping the pattern: the pattern stays, the panel shows the memory in
      use, and Wrap Edges, the Engine submenu and the automaton choice are greyed out.
- [ ] On the plane, open the Gosper glider gun, set the step to 2^10 with F8 or `}`, and run at Max:
      the stream of gliders grows by 1024 generations a step, and Fit shows all of it, zoomed out.
- [ ] Set the step to 2^30 on a busy pattern: the status bar says "Computing…", and the window still
      pans, zooms and repaints. Pause, draw or Clear during the step: it stops at once.
