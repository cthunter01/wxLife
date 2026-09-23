# Architecture

This page is for anyone who wants to read or change the code. It covers:
- how the layers fit together and who owns each piece of state;
- how a click becomes a redraw;
- why the simulation timer works the way it does;
- where the code is meant to be extended.

The design goals, in order: the code is easy to study, easy to extend, and fast enough. "Fast enough"
means a 1000 × 1000 world runs smoothly at every cell size from 1 to 100 px, and a 10000 × 10000 world
stays usable.

All numbers below were measured on the development machine unless marked otherwise: an i7-12800H
(a hybrid CPU with 6 performance and 8 efficiency cores, 20 threads), GCC 16, Release build.

## Layers

```
+--------------------------------------------------------------------------+
| app/     LifeApp (wxApp, owns the World)                                 |
| ui/      MainFrame, WorldCanvas, ControlPanel, WorldSizeDialog,          |
|          DemoDialog, SimulationRunner, MenuBar, CommandIds, Defaults,    |
|          Theme, WxConvert                  lib wxLife_ui (wx::core, base)|
+--------------------------------------------------------------------------+
| render/  Types, Viewport, PixelBuffer, RenderStyle, Rasterizer,          |
|          Thumbnail                                                       |
| core/    Types, Line, Rule, Ant, Grid, Random, ParallelBands, Stepper,   |
|          ReferenceStepper, StepKernel, BandedStepper, World, HashLife,   |
|          WorldLimits, Speed, Pacer, Format, Pattern, PatternSetup,       |
|          Demo, EmbeddedFile (+ the generated EmbeddedPatterns.cpp)       |
|                                             lib wxLife_lib (+ Threads)  |
+--------------------------------------------------------------------------+
src/Main.cpp:  exe wxLife -> wxLife_ui
tools/bench:   wxLife_bench -> wxLife_lib
tests:         wxLife_tests -> wxLife_lib, wxLife_ui
```

Each namespace (`wxLife::core`, `wxLife::render`, `wxLife::ui`, `wxLife::app`) has its headers in one
directory under `include/wxLife/` and its sources in the directory of the same name under `src/`. The
two layers without wx share the library `wxLife_lib`, and the two with wx share `wxLife_ui`. The rules:

1. **`core`** uses only the standard library and threads.
   - It never reads a clock: the pacing functions take the time as an argument.
   - Its results depend only on its inputs. The number of threads never changes a result.
2. **`render`** uses `core`. It works in device pixels and never includes wx.
3. **`ui`** and **`app`** are the only layers that include `<wx/...>`. `app` needs only `<wx/app.h>`
   and `<wx/utils.h>`.
4. **`app`** holds the `wxApp`. `src/Main.cpp` only instantiates it with `wxIMPLEMENT_APP`.
5. **No layer includes a layer above it.** Two things enforce this:
   - `wxLife_lib` links neither wx nor `wxLife_ui`, so `core` and `render` cannot use wx.
   - The `layering` test (`cmake/CheckLayering.cmake`) searches the include lines of the `core`,
     `render` and `ui` headers and sources for forbidden directories, in every spelling (`# include`,
     `<...>` or `"..."`).
6. **Include paths name the layer,** for example `#include "wxLife/core/World.h"`, so every include
   line shows which layer it uses.

All tests are in one program, `wxLife_tests`. Everything with real logic is below the wx layer, so the
unit tests need no display. The GUI smoke tests drive a real `MainFrame`; they start wx only when they
run, so a test process that runs only unit tests never opens the display.

## Who owns what

Every other place that shows one of these values is only a view of it.

| State | Owner |
|---|---|
| World kind, cells (a grid or a HashLife plane), automaton, ants, rule, topology, engine, generation, population | `core::World`, held by `app::LifeApp` |
| Running flag, speed, step size, pacing, measured rate, the step running in the background | `ui::SimulationRunner`, a member of `MainFrame` |
| Scale (cell size, or cells per pixel below 1 px), scroll offset, grid-line flag, colours | `ui::WorldCanvas`, through `render::Viewport` and `render::RenderStyle` (colours from `wxLife/ui/Theme.h`) |
| Random-fill density, rule text being edited, preset selection | The `ui::ControlPanel` widgets |
| Memory budget | `MainFrame`, computed once with `core::defaultMemoryBudget()` |
| The demo chosen last, the folder File → Open looked in last | `MainFrame` (`m_lastDemo`, `m_lastPatternDir`) |
| Start-up values | `wxLife/ui/Defaults.h` |

**Only two classes change the World,** and both run on the UI thread:
- `MainFrame` changes it in response to user commands: clear, randomize, `setCells`, resize, rule,
  topology, engine, and loading a pattern (`loadPattern()`).
- `SimulationRunner` changes it by calling `World::step()` on a fixed-size world and
  `World::stepPlane()` on an unbounded one. The second call runs on a worker thread; see Unbounded
  worlds below for the rules that makes necessary.

`WorldCanvas` only reads the World. It hands mouse strokes to `MainFrame` through its `paintCells`
callback.

**Lifetime.** `LifeApp` owns the World because wx destroys every window before it deletes the app
object. The World therefore outlives every window that holds a reference to it.
- Child windows are held through raw pointers that do not own them; wx deletes them.
- Everything else is held by value or through `std::unique_ptr`.

## The simulation (`core`)

**Storage.** A `Grid` is one byte per cell, with a one-cell *ghost border* around it.
- Neighbour sums therefore need no edge checks.
- Before every step, `Grid::updateBorder()` refreshes the border in O(width + height):
  - **Bounded:** the border is filled with dead cells.
  - **Torus:** the border gets copies of the opposite edges. The order of the copies matters: first the
    ghost columns of every row, then whole padded rows, so the four corners come out right.
- `World` holds two grids and swaps them after every generation, so stepping never allocates grid
  memory. (Starting the band threads still allocates; see Threads.)
- A world needs `worldBytes() = 2 × (W + 2) × (H + 2)` bytes.

**Rules.** A `Rule` is two 9-bit masks, one for birth and one for survival. Bit *n* is set when the rule
applies to a cell with *n* live neighbours.
- `Rule::parse()` is `constexpr`, so a typo in `kRulePresets` fails to compile.
- The kernel sums the 3 × 3 block *including* the centre cell. `Rule::kernelMasks()` therefore shifts
  the survival mask left by one, which removes a branch and a lookup table from the hot loop.

**Engines.** `Stepper` is the interface. `makeStepper()` builds an engine from a `StepperKind`,
`kStepperKinds` lists every kind, and `parseStepperKind()` reads a kind's name.
- **`ReferenceStepper`** counts the eight neighbours of every cell and applies explicit edge rules. It
  ignores the ghost border. It is slow on purpose: it is the executable specification and the test
  oracle.
- **`BandedStepper`** is the production engine:
  1. It splits the rows into bands with `suggestedBandCount()`: one band per 125,000 cells
     (`kMinCellsPerBand`), but a single band below 500,000 cells (4 bands' worth), and at most one band
     per hardware thread.
  2. It sizes one scratch row per band before any band starts, so the band jobs never allocate.
  3. It runs `kernel::stepRow()` on every row through `forEachBand()`.
  4. It adds up the band populations.
- **`kernel::stepRow()`** (`src/core/StepKernel.cpp`) is the only hot loop.
  - It has two passes: vertical 3-sums, then 3 × 3 sums and the mask lookup.
  - `target_clones` builds it for AVX2 and for baseline x86-64, and the dynamic loader picks the right
    one. This needs x86-64 and glibc (musl has no ifunc), so macOS, Windows and ARM get the single version
    the compiler's default target allows. With `WXLIFE_KERNEL_CLONES=OFF`, as in the `headless` preset,
    only the baseline version is built.
  - With GCC and Clang, the file is compiled with `-O3` in every build type.

**Automata.** `World::step()` switches on `Automaton`, and the switch has no `default`, so `-Wswitch`
lists every place a third automaton would need.
- **`Automaton::LIFE`** is the path above: refresh the border, run the stepper into `m_next`, swap.
- **`Automaton::LANGTON_ANT`** (`wxLife/core/Ant.h`, header-only) moves each ant of `m_ants` once, in
  index order, straight on `m_current`. There is no border to refresh and no buffer to swap, and each ant
  therefore sees what the ones before it have just left. One move is: turn right on a dead cell or left
  on a live one, flip that cell, step forward. `advance()` returns the ±1 the population changed by, so
  the counter stays exact without a recount.
- An ant **always wraps**, whatever `Topology` says, because an ant that walked off a bounded edge would
  have to be deleted while a wrapped one keeps drawing. The UI therefore greys out Wrap Edges, the rule
  and the engine while the ant runs, so no control silently means nothing.
- `m_next` is left allocated but idle in ant mode. It keeps `worldBytes()`, `validateExtent()` and
  `resize()` untouched, so the memory budget does not move under the user when the automaton does.
- Every ant is always inside the world. `setAnts()`, `resetAnts()`, `toggleAntAt()` and `resize()` are
  the four places that keep it that way, and it is what makes `advance()`'s precondition hold. `World`
  reserves room for `kMaxAnts` up front, so adding an ant never reallocates and `toggleAntAt()` is
  `noexcept`.
- Generation 0 means the ants have not moved yet, so `clear()`, `randomize()` and a `resize()` that
  keeps nothing lay them out again.

**Threads.** `forEachBand()` is the only code that creates threads.
- Band 0 runs on the calling thread and the other bands on `std::thread`s, which it joins. (Not
  `std::jthread`: Apple's libc++ has it only from LLVM 20.) A band whose thread cannot be started runs
  on the calling thread instead.
- It returns only after every band has finished.
- Bands read `src` and write different rows of `dst`, so they need no locks.

Because every call joins its threads before it returns, the UI thread is the only thread that ever sees
a fixed-size World. The one exception is the HashLife step of an unbounded world, which
`SimulationRunner` runs on a thread of its own (see Unbounded worlds below).

Starting the threads on every step keeps the code simple, and it is fast enough:

| World | Bands | ms per generation |
|---|---|---|
| 1000² | 1 | 0.30 |
| 1000² | 2 | 0.38 |
| 1000² | 4 | 0.22 |
| 1000² | default (8) | 0.16 |
| 10000² | 1 | 31 |
| 10000² | default (20) | 5.5 |

Two bands are slower than one. On this hybrid CPU a short-lived helper thread often lands on an
efficiency core, which is more than twice as slow (one band of 1000² takes 0.67 ms there and 0.29 ms on a
performance core), and every step waits for its slowest band. That is why `suggestedBandCount()` does not
split a world with fewer than 4 bands' worth of cells. Starting a helper thread also costs about 10 µs,
so at 1000² the 7 helpers take almost half of each step. A persistent thread pool avoids both costs: in
a prototype, even 2 bands were faster than 1 (see Extension points below).

**World operations.**
- **`step()`** is the only function that increments the generation.
- **`clear()`**, **`randomize()`** and **`resize(…, false)`** reset the generation to 0. Edits, rule,
  topology and engine changes leave it alone.
- **The population** is kept up to date incrementally: `step()` returns it, `setCells()` counts only
  the cells that actually change, and `randomize()` and `resize()` recount it.
- **`randomize()`** gives every row its own `SplitMix64`, seeded with a hash of the seed and the row
  number. The result is therefore the same for any number of bands, and the rows are independent even
  for small seeds.
- **`resize()`** allocates both new grids before it changes anything (the strong exception guarantee).
  With `keepPattern` it copies the overlapping block, centred.
- **`validateExtent()`** checks a size against the side limits (1 to 100,000) and the memory budget.
  The budget is a quarter of the RAM, clamped to [256 MiB, 16 GiB]. `physicalMemoryBytes()` is the one
  OS query in `core`: `sysconf()` on Linux and macOS, `GlobalMemoryStatusEx()` on Windows. Under Linux's
  default memory
  overcommit, allocating a world within the budget practically never fails, so this check, not
  `std::bad_alloc`, is what keeps a world within the machine's memory.

**Timing values.**
- `Speed` is a plain value: 1 to 1000 generations per second, or unlimited.
- `GenerationPacer` turns elapsed time into generations owed.
- `RateMeter` measures the achieved rate over windows of at least 0.5 s. A window opens and closes only
  on ticks that stepped, so it always spans whole generations. There is no value until the first window
  has closed, and until then the status bar shows only the target.

The pacer and the meter take the time as an argument, so their tests never sleep.

## HashLife (`core`)

`HashLife` (`wxLife/core/HashLife.h`) runs a B/S rule on an unbounded plane with Bill Gosper's
algorithm. It is the engine behind the unbounded worlds (next section), and it knows nothing of them:
it only takes cells, rules and steps.

**The quadtree.** A node of level k is a 2^k × 2^k square: four children one level down (NW, NE, SW,
SE), or a single cell at level 0. Nodes are hash-consed, so each distinct square exists once and is
never changed; empty space and repeated structure cost almost nothing. Positions are 64-bit, with
(0, 0) at the centre of the root.
- Nodes are numbered with 32-bit ids and stored in chunks of 65,536 that are reserved up front and never
  move. Ids 0 and 1 are the dead and the live cell.
- An open-addressing hash table finds a node by its four children.
- Each node carries its population, so empty regions are skipped and the total is always known.

**A step.** Each node remembers its *result*: its centre half, advanced 2^min(j, k - 2) generations
for the current step size 2^j. `successor()` builds it from the nine overlapping squares one level
down: their results, then either their results again (a full step) or their centres (a smaller one).
Level 2 is the base case, a 65,536-entry table from 4 × 4 cells to the centre 2 × 2 one generation on.
- Before a step of 2^j generations, the root grows until it is at least level j + 3 with every live
  cell in its centre quarter. A pattern can grow one cell per generation (Seeds and Replicator do), so
  this is what guarantees that the result, the centre half, holds everything the step makes.
- Results depend on j, so changing the step size forgets them all.
- Afterwards the root shrinks back while the pattern fits a smaller one.
- The universe ends at level 62. A pattern that drifts 2^59 cells from the centre stops with
  `UNIVERSE_EDGE`; a glider needs 2^61 generations for that.

**Memory.** The memory budget sets the most nodes there may be. When three quarters are in use,
`collectGarbage()` keeps what the root needs, moves the survivors down in id order (children are
always older than their parents, so one pass renumbers everything) and forgets every result. A step
that runs out of nodes throws internally, is undone, collects and tries once more; then it reports
`OUT_OF_MEMORY` and nothing has changed, and a smaller step may still fit. The chunks are only ever
added, into a table of chunk slots sized once for the budget, so a node never moves except in
`collectGarbage()`.

**Stepping on another thread.** `step()` takes `HashLifeStepOptions`:
- `cancel` points at a flag that `successor()` checks at every node of level 6 or more. Once it is
  set, the step throws internally, is undone as for a lack of memory, and reports `CANCELLED`. The
  results it has already worked out stay, so the next step starts ahead.
- `collectGarbage = false` forbids the step to collect: it reports `OUT_OF_MEMORY` at once, and the
  owner collects between steps. Collecting moves nodes, which readers on other threads must never see.

With both, one thread may step while others read `population()`, `generation()`, `at()`, `bounds()`
and `forEachBlock()`. They see the plane before the step until it publishes its new root and
generation, both atomic, and the plane after it from then on. A step only appends nodes nobody can
reach yet and writes the remembered results, which readers never look at. The node count and
`memoryBytes()` belong to the stepping thread while it runs. `HashLifeTest` runs a step against three
reading threads, and the `tsan` preset checks that under ThreadSanitizer.

**Editing and reading.** `setCells()` builds a quadtree of the new cells and unites it with the
plane (or subtracts it, to erase). `forEachBlock()` visits the 2^s × 2^s blocks that hold a live cell
in a rectangle, skipping empty nodes, so a renderer showing 2^s cells per pixel costs one call per lit
pixel. `bounds()` finds the extreme live cells, remembering each shared node's answer.

**What it buys.** On this machine (Release), the Primer reaches generation 1,245,184 in 3.7 s. Its
1,272 output spaceships are then exactly the primes up to 10,369, where a bounded world goes wrong
after 67. The Gosper gun jumps 2^20 generations in 5 ms. Chaotic soups gain nothing: for them the
dense engine is faster.

## Unbounded worlds (`World`, `SimulationRunner`, `Viewport`)

**Two kinds of world.** `WorldKind` is `FIXED_SIZE` or `UNBOUNDED`. A fixed-size `World` holds its two
grids; an unbounded one holds a `HashLife` (`m_plane`) instead, and its grids are released.
- `makeUnbounded(keepPattern, budget)` turns a world into a plane, the grid's centre cell landing at
  (0, 0); `resize()` turns a plane back into a grid, the plane's (0, 0) becoming the grid's centre cell.
  Both keep the generation when they keep the pattern, and both have the strong exception guarantee.
- The plane gets the same memory budget as a grid would, as a node limit.
- A plane runs only Life, and only rules without B0, which would fill the plane in one generation.
  `MainFrame::applyRule()` refuses a B0 rule with the panel's error line, and `WorldSizeDialog` refuses
  Unbounded for B0 rules and for Langton's ant with the reason (`MainFrame::unboundedRefusal()`). The
  ant, Wrap Edges and the engines are greyed out while a plane runs.
- Positions of cells are `UniversePos` (64-bit) wherever the UI talks about cells: the canvas callbacks,
  the hovered cell, strokes (`forEachCellOnLine()` is a template over both position types) and
  `World::setCells(span<const UniversePos>)`. `World::cellAt()` reads any cell of either kind.
- Randomize fills the cells the view shows (at most `kMaxWorldSide` on a side), since a plane has no
  whole to fill. File → Open into a plane keeps the plane and centres the pattern on (0, 0)
  (`fileSetup()` takes the current kind). Demos bring the fixed-size worlds they were tuned for.

**Stepping.** A plane steps 2^k generations at a time, where k is the step exponent the user sets (the
panel's Step box, F7 and F8, `{` and `}`; at most `SimulationRunner::kMaxStepExponent`, 40). Every
step runs on a worker thread, so a step of seconds or minutes never blocks the UI:
1. `launchStep()` collects garbage on the UI thread if the plane is full (`World::collectIfFull()`),
   then starts a `std::thread` that calls `stepPlane(k, {.cancel, .collectGarbage = false})`, stores
   the result and sets `m_finished` (release). If no thread can be started, it steps right there.
2. The one-shot timer keeps ticking, but a tick now only checks `m_finished` (acquire). While the step
   runs, a tick every `kBusyReport` (250 ms) reports zero generations, so the status bar can say
   "Computing…".
3. When the step has finished, the next tick joins the thread, commits 2^k generations to the pacer
   and the rate meter, reports them, and starts the next step if the pacer says it is due.
   `GenerationPacer::setStepSize()` lets the debt grow to at least one step, so a step of 2^20
   generations comes due even at 1 generation per second (after 2^20 seconds).
4. `OUT_OF_MEMORY`: the runner collects garbage on the UI thread and tries the step once more. If that
   fails too, or the pattern reached the edge of the universe, the runner stops and `MainFrame` shows
   the reason in a "Simulation Stopped" box.

While a step runs, the UI thread only reads the world: painting, the status bar, the hovered cell. Every
command that changes the world calls `SimulationRunner::interrupt()` or `stop()` first, which sets the
cancel flag and joins the worker; a cancelled step changes nothing. Running then goes on at the next
tick. The panel's memory line (`memoryBytes()`) is refreshed only between steps, because the node count
belongs to the worker while a step runs.

**The view.** `Viewport::setUnbounded()` removes the edges but the universe's: the offset may go
`kUnboundedReach` (2^55) content pixels either way from (0, 0), which keeps every product in the zoom
arithmetic within 64 bits and still spans 3.6 × 10^16 cells at 1 px. Below 1 px the reach is also
capped at the universe, `core::kUniverseRadius` (2^61) cells either way, so zooming out ends with the
whole universe in view, centred like a small fixed-size world, with nothing beyond it. `cellAt()`,
`cellAtClamped()`, `cellOrigin()` and `visibleCells()` use `UniversePos` and `UniverseRect`. Fit shows
the plane's bounding box (an empty plane is centred on (0, 0)), and Center World centres it. The
scrollbars stay visible, so the canvas keeps its size, but they do nothing on a plane.

**Rendering.** `Rasterizer::render(const HashLife&, …)` copies the visible cells into a window `Grid`
(`m_window`, kept between frames) with `forEachBlock()`, then paints that grid with the same stamps and
row copies as a fixed-size world, with the window's corner as its origin. Below 1 px it asks
`forEachBlock()` for blocks of 2^shrink cells, which are aligned exactly as the pixels are, so the
window holds one entry per pixel. The cost is the canvas plus one visit per lit pixel; empty space is
skipped by the quadtree, at any scale.

## Patterns and demos (`core`, `DemoDialog`)

**Pattern files.** `readPattern()` (`wxLife/core/Pattern.h`) reads RLE and plaintext (`.cells`) text into
a `Pattern`: the live cells relative to the pattern's top-left corner, its extent, rule, name, author and
comments. It recognises the format from the first line that is not blank, and it refuses what it cannot
run with a `PatternError` that names the line: macrocell and Life 1.0x files, multi-state cells, rules
that are not two-state B/S rules, and patterns wider or taller than `kMaxWorldSide`. A header that
declares such a size is refused before a single cell is read, so the 30 MB Caterpillar file fails at
its second line. `core` reads no files; `MainFrame` reads the bytes with `wxFFile` and hands them over.

**The embedded files.** The demos' RLE files live in `patterns/`, listed in `src/CMakeLists.txt`.
`cmake/EmbedPatterns.cmake` turns them into one generated source of `char` arrays (string literals would
hit MSVC's 64 KiB limit) behind `embeddedPatternFiles()` (`wxLife/core/EmbeddedFile.h`). The program
therefore needs no data files, and no platform-specific way to find them. The generated source is
rewritten only when a pattern changes, and clang-tidy skips it (`SKIP_LINTING`).

**The catalogue.** `demos()` (`wxLife/core/Demo.h`) is a table of `Demo`s: name, category, credit,
description, file, and the world the demo runs in, which is its size, topology, where the pattern goes,
speed, the part of the world to show first, and for the ant demos the automaton and the ants. Every
setting was chosen by running the demo headlessly:
- A glider that reaches a dead edge vanishes cleanly, so the guns fire forever in bounded worlds.
- The methuselahs' worlds are large enough that their evolution, apart from the escaping gliders, is
  cell for cell the same as on an unbounded plane.
- The prime calculators throw streams of spaceships up and to the right. An unbounded plane swallows
  them; a bounded or wrapping world sends wreckage back, which reaches the machine after roughly four
  times the distance to the edges in generations. Their worlds are as large as a quick load allows, and
  their descriptions say how many primes come out right, as measured.

`DemoTest` checks the table against the files, and pins a few descriptions to the real patterns: the
Gosper gun's period, the Primer's first primes and the ant demos' promises.

**Loading.** `PatternSetup` (`wxLife/core/PatternSetup.h`) is everything a load changes. `demoSetup()`
makes it from a `Demo`; `fileSetup()` makes it for a file: the pattern in the middle of a world with
half its size of room on each side (at least `kMinFileMargin`), halving the room until the world fits the
memory budget. `MainFrame::loadPattern()` then applies it in one place: stop the runner, resize or clear
the world and call `worldExtentChanged()` at once (as a resize does), set the topology, the rule, the
ants and the automaton, set the cells with `World::setCells(cells, kAlive, origin)`, set the speed, and
show `setup.view` with `WorldCanvas::showCells()`. The world waits at generation 0.

**The view.** `Viewport::fitCells()` is `fitWorld()` for part of the world: the largest cell size that
shows the cells, centred on them as far as clamping allows. `WorldCanvas` keeps whatever it last fitted
(`m_keptFit`, the whole world for `fitWorld()`) through canvas size changes, as it always kept the fitted
world, so a demo's view survives the window settling into its size.

**The dialog.** `DemoDialog` lists the categories and demos in a `wxTreeCtrl`. For the selected demo it
shows the credit, a preview drawn by `render::drawThumbnail()`, the pattern's size, the world and the
description. A demo over the memory budget is listed, but Load is disabled and the dialog says why;
`Validate()` refuses Enter too, as `WorldSizeDialog` does. Patterns are read when first shown and kept
for the dialog's lifetime.

## Rendering and the camera (`render`, `WorldCanvas`)

**Units.** Everything on the canvas is measured in *device* pixels (`render::Pixel`, 64-bit). A cell
size of 1 is therefore one physical pixel, even on a HiDPI screen. `wxLife/render/Types.h` holds the
pixel types and `Rgb`, as `wxLife/core/Types.h` does for cells.
- `WorldCanvas::deviceClientSize()` and `toDevice()` multiply wx's logical coordinates by
  `GetContentScaleFactor()`.
- The bitmap carries the same factor, so wx draws it 1:1 on the physical display.
- wx reports mouse positions in whole logical pixels, so at a scale factor of 2 the pointer maps only to
  even device pixels. Cells smaller than the scale factor can then be clicked only in every second row
  and column. Drawing is not affected.

**`Viewport`** is the camera, and the only source of truth for zoom and scroll position.
- **Scale.** `render::Scale` is either a cell size of 1 to 100 px, or below 1 px a `shrink`: one pixel
  for each block of 2^shrink × 2^shrink cells. Powers of two keep the blocks aligned with HashLife's
  quadtree, and every conversion exact.
- **Offset.** `offset()` is the content pixel shown at the canvas's top-left corner: content pixel p
  shows cell ⌊p / size⌋, or below 1 px the block starting at cell p × 2^shrink. On an axis where the
  world is smaller than the canvas, `clampOffset()` centres the world, so the offset can be negative.
  Conversions between pixels and cells therefore use `floorDiv()`.
- **Anchored zoom.** `setScale()` keeps the world point under the centre of the anchor pixel inside
  that pixel (at most half a pixel off), so the cell under the anchor stays, unless clamping moves the
  view. It uses only integers: the point is a cell plus a fraction (2 × (p mod size) + 1) / (2 × size)
  of one, and below 1 px, where a pixel is an even number of cells wide, the corner between two cells.
  On each axis, zooms at the same anchor, with nothing else moving the camera in between, form a run
  that keeps the point of its first zoom (`m_zoomRuns`). Rounding errors therefore never add up, and
  going back to a scale restores its offset. Clamping an axis, which includes centring a world smaller
  than the canvas, ends that axis's run only.
- **Zoom steps.** `zoomBy()` moves along a ladder: the shrink levels from `maxShrink()` up to 1, then
  the `kZoomSteps` entries. `maxShrink()` is the first shrink at which the whole world, for a plane
  the whole universe, fits the canvas, so zooming out ends with everything in view; a world that fits
  at 1 px never goes below it. A view left beyond the limit by a growing canvas may zoom in, never
  further out.

**`Rasterizer::render()`** fills a `PixelBuffer` the size of the canvas. Its cost depends only on the
number of canvas pixels, never on the size of the world.
1. Once per frame it builds four cell *stamps* (alive or dead, each with or without a major grid-line
   pixel) and two grid-line rows.
2. For each visible cell row, it writes the first visible pixel row from the stamps. It then copies
   that pixel row down the rest of the cell. When grid lines are shown, the cell's last pixel row gets
   a grid-line row instead.
3. At 1 px per cell it writes one colour per pixel instead of copying stamps.

Grid lines use the last pixel column and row of each cell, and they appear only from 5 px. A crossing
uses the major colour when either of the two lines is major.

**Below 1 px** a pixel shows a block of cells and is alive when any of them is, as in Golly: a lone
glider stays visible, and dense areas look solid. `shrinkGrid()` makes a grid of blocks, one per
visible pixel, which `paint()` then draws as 1 px cells, so the pixel path is the same. For each row of
blocks it ORs the block's cell rows together (vectorised byte ORs), then ORs each block's run of that
row; the rows of blocks are split into bands on `forEachBand()`'s threads. Unlike the other paths its
cost grows with the visible cells, but it is bound by memory bandwidth. Measured with a 25% random
world on a 1080p canvas: all of 10000² (1/16 px) takes 2.6 ms per frame, and all of 20000² (1/32 px)
7.4 ms, less than one generation of stepping it.

The rasterizer is compiled with `-O3` in every build type. Its times, in ms per frame, for a 25% random
10000² world with grid lines on:

| Canvas | 1 px | 2 px | 5 px | 100 px |
|---|---|---|---|---|
| 1080p | 1.0 | 0.9 | 0.3 | 0.14 |
| 4K | 3.8 | 3.8 | 1.3 | 0.65 |

The cell stamps are picked by indexing a 2 × 2 table, not by branching: with random cells at 2 px, a
branch per cell doubled the frame time.

**`WorldCanvas::onPaint()`** draws the frame.
1. It wraps the frame's bytes in a `wxImage` without copying them.
2. It converts the image to a `wxBitmap` that carries the scale factor, and draws it with one
   `DrawBitmap`.

That blit costs 2.2 ms at 1080p and 9.1 ms at 4K; converting GTK's pixbuf to a cairo surface takes
most of that time. Writing through `wxNativePixelData` directly was measured and is no faster.
- There is no erase step (`wxBG_STYLE_PAINT`) and no `wxBufferedPaintDC`: GTK 3 already double-buffers.
- Every redraw request is `Refresh(false)`, and GTK's frame clock merges the requests into at most one
  paint per display frame.

**Scrollbars.** The canvas is a plain `wxWindow` with `wxALWAYS_SHOW_SB`. `wxScrolledWindow` would not
work: its virtual-size model cannot express anchored zoom or the negative offset of a centred world.
- Scrollbar units are device pixels. `syncScrollbars()` pushes the viewport into them, and `onScroll()`
  maps the scrollbar events back.
- Scrollbars that are always shown keep the client size stable. With automatic scrollbars, the client
  size was measured changing without any size event.

**Fit.** `fitWorld()` fits the world into the canvas and *keeps* it fitted (`m_keepFitted`) until the
user moves the camera (`cameraMoved()`).
- The window gets its final size, and under Wayland its final scale, only after the first frame. A
  one-time fit would therefore use the wrong size.
- `worldExtentChanged()` fits again after every resize.

## From a click to a redraw

Every command except Quit and About (which use wx's stock ids) is a `CommandId`. Camera moves and
strokes are not commands; they stay inside `WorldCanvas`.
- Menu items send their id.
- Panel controls re-send their own events as ids (`sendOn()` in `src/ui/ControlPanel.cpp` calls
  `emitCommand()`).
- The canvas's single-key shortcuts call `emitCommand()`.

Command events travel up the window tree, so all of them reach `MainFrame`. `MainFrame::bindCommands()`
is the one table that maps ids to handlers. After a command from a panel button or check box, clicked or
pressed from the keyboard (except Apply), it gives the focus back to the canvas, because GTK leaves it on
the clicked control and Space would press that control again.

```
[Run] click → wxEVT_BUTTON on the button → emitCommand(ID_RUN_PAUSE)
  → wxEVT_MENU propagates: button → static box → ControlPanel → MainFrame
  → MainFrame::onRunPause → m_runner.toggle() → syncControls() → updateStatusBar(true)

timer tick → SimulationRunner::onTimer → m_pacer.plan(now) → m_world.step() × N
  → m_pacer.commit(N), m_meter.record(N, now) → MainFrame::onSimulationTick
  → m_canvas->Refresh(false) + updateStatusBar(false) → scheduleNext() (idle gap ≥ 4 ms)

timer tick, unbounded → SimulationRunner::stepInBackground → step finished? (m_finished)
  → no: report 0 generations every 250 ms ("Computing…") → scheduleNext()
  → yes: join → m_pacer.commit(2^k) → MainFrame::onSimulationTick → due? launchStep()
    → collectIfFull() → std::thread: m_world.stepPlane(k, cancel, no collection) → scheduleNext()

GTK frame clock → WorldCanvas::onPaint → syncCanvasSize()
  → Rasterizer::render(world.cells(), viewport, style, frame), or render(world.plane(), …)
  → in ant mode, render::drawAnts(world.ants(), viewport, style, frame) over it
  → wxImage (borrowed bytes) → wxBitmap(image, depth, scale) → DrawBitmap

left drag → WorldCanvas::onMouse → continuePaint → Viewport::cellAtClamped → forEachCellOnLine
  → m_callbacks.paintCells(segment, value) → MainFrame::onPaintCells → m_world.setCells
  → worldContentChanged() → m_canvas->Refresh(false) + updateStatusBar(true)

Ctrl+left click → WorldCanvas::onMouse → m_callbacks.toggleAnt(cell) → MainFrame::onToggleAnt
  → m_world.toggleAntAt(cell) → syncControls() (the panel's ant count follows) → worldContentChanged()

Ctrl+wheel → WorldCanvas::onWheel → Viewport::zoomBy(steps, pointer) → cameraMoved()
  → viewportChanged() → syncScrollbars(), Refresh(false), m_callbacks.viewChanged
  → MainFrame::onViewChanged → m_panel->setScale(scale, maxShrink), updateStatusBar(true)

G key on the canvas → WorldCanvas::onKeyDown → emitCommand(ID_TOGGLE_GRID)
  → the same MainFrame::onToggleGrid as the menu item and the check box

[Demos…] or File → Demo Patterns… → MainFrame::onDemoPatterns → DemoDialog::ask (modal)
  → core::demoPattern(demo) + core::demoSetup() → MainFrame::loadPattern
  → World::resize (or clear) + WorldCanvas::worldExtentChanged() → topology, rule, ants, automaton
  → World::setCells(cells, kAlive, origin) → runner speed → WorldCanvas::showCells(view)
  → syncControls() + worldContentChanged()
```

**Strokes.**
- A press paints the pressed cell at once. Every later mouse motion paints the line from the previous
  cell to the cell under the pointer, as one segment and one edit, so fast drags leave no gaps.
- When the camera moves during a stroke (fit, centre, zoom, wheel, arrow keys, scrollbars),
  `viewportChanged()` forgets the previous cell. The next motion then paints only the cell under the
  pointer and goes on from there, so no line crosses cells the pointer never touched.
- Below 1 px per cell a pixel is a block of cells, so every press there starts a pan instead, and a
  stroke already under way (the wheel zoomed out during it) paints nothing until the view is back at
  1 px or more.
- The canvas captures the mouse for the whole drag. Only the release of the button that started the drag
  (`m_dragButton`) ends it.
- The capture is released in one place, `endDrag()`, which also handles capture loss, Esc and
  `cancelStroke()`. Ending a stroke keeps the cells it has painted.

**Resizing the world** (`MainFrame::onWorldSize`):
1. Remember whether the simulation was running, stop it, and cancel any stroke.
2. Call `WorldSizeDialog::ask()`, passing the current kind and size, `cellsThatFit()` for the "Fit
   window" preset, the budget, and why an unbounded world is impossible, if it is
   (`unboundedRefusal()`). The dialog checks the typed text of both boxes, not `wxSpinCtrl`'s clamped
   value, and its `Validate()` refuses OK and Enter until the size, or the choice of Unbounded, is
   valid.
3. Check the answer again with `validateExtent()`, or the refusal. Inside a `try` block, call
   `m_world.resize()` or `m_world.makeUnbounded()`, then `m_canvas->worldExtentChanged()` at once, so no
   paint ever sees a viewport with the old extent or kind. On `std::bad_alloc`, show a message: the old
   world is still intact. (Under Linux's default overcommit this is rare; see `validateExtent()` above.)
4. If the Reference engine is active and the world is now larger than
   `ReferenceStepper::kRecommendedMaxCells`, switch to Banded.
5. Restart the simulation if it was running, then call `syncControls()` and `updateStatusBar(true)`.

**Keeping the controls in step.**
- `syncControls()` pushes model values into the panel and the menus: check marks, the enabled state of
  the Reference engine, and the Run/Pause label.
- The panel's setters use `SetValue`, `SetSelection` and `ChangeValue`, which send no events, so there
  are no feedback loops.
- `syncControls()` never touches the rule text, so text the user is still typing survives.

**Status bar.**
- Simulation ticks update it at most once per `defaults::kStatusRefresh` (100 ms).
- Every user action forces an update, so the display is never stale while paused.
- The speed field shows the target. While the simulation runs, the measured rate follows once
  `RateMeter` has one.

**Start-up** (`LifeApp`):
1. `Initialize()` sets `GTK_OVERLAY_SCROLLING=0`, unless the user has already set it. This has to happen
   before `wxApp::Initialize()` starts GTK, which starts other threads, and `setenv()` is not
   thread-safe.
2. `OnInit()` parses the command line.
3. It creates the World (512², torus, Conway).
4. It creates the `MainFrame`. Its constructor sets up the menus, the status bar, the layout and the
   command table, sets the speed, randomizes the World, syncs the controls, calls `fitWorld()` and gives
   the canvas the focus.
5. It shows the frame. The simulation starts paused.

**Shutdown.**
1. `MainFrame::onClose` stops the runner and cancels any stroke. wx asserts if a window is destroyed
   while it holds the mouse capture.
2. wx destroys the windows, then the app object, which deletes the World last.

## The timer and redraw priority

`SimulationRunner` owns the only timer. The timer is **one-shot**: it is re-armed at the end of every
tick, always leaving at least `kMinIdleGap` (4 ms) of idle time before the next tick. A repeating timer
does not work here.

**Why a repeating timer fails.**
- wxGTK's `wxTimer` is a GLib timeout at the default priority (0).
- GTK repaints at `GDK_PRIORITY_REDRAW` (120). In GLib a lower number wins, so the timer always goes
  first.
- A repeating timer whose handler takes longer than its interval is always ready, so GTK never gets to
  repaint.

The effect was measured headlessly:

| Timer | Work per tick | Result |
|---|---|---|
| Repeating, 16 ms | 20 ms | **0 repaints in 2 s** |
| One-shot, re-armed with a gap of at least 4 ms | 20 ms | 82 repaints in 83 ticks |
| Either kind | 10 ms | About 60 ticks per second |

The idle gap therefore costs no throughput.

**The tick loop** (`SimulationRunner::onTimer`):
1. `GenerationPacer::plan()` adds the elapsed time × rate to the debt of generations owed.
2. The runner steps until it has done every whole generation owed, or until `kTickBudget` (10 ms)
   has passed. When a generation is owed, it takes at least one step, even if that step alone takes
   longer than the budget.
3. `commit()` subtracts the generations stepped.
4. The runner reports to `MainFrame` and re-arms the timer after `max(4 ms, 16 ms − tick cost)`.

The debt is capped at 250 ms worth of generations (`kMaxCatchUp`). A stall, such as a modal dialog, is
therefore not followed by a burst of catch-up steps. When ticks run out of time, the rate settles at
whatever the machine can manage. A paused app uses no CPU, because the timer is not armed.

**Rates this gives:**

| Case | Generations per tick | Result |
|---|---|---|
| Target under about 60 gen/s | 0 or 1 | One repaint per generation |
| 1000², Max | About 60 (10 ms ÷ 0.16 ms) | One repaint per tick, about 60 per second (estimated) |
| 10000², Max | 2 (about 5.5 ms each) | Measured headlessly: 90 to 116 gen/s at 43 to 50 repaints per second; input stays responsive |
| One generation takes 10 ms or more (here from about 14000², 200 million cells) | 1 | Fewer repaints, but input is still handled between ticks |

The last row is the limit of stepping on the UI thread, which fixed-size worlds still do. Unbounded
worlds step on a worker thread instead, one step of 2^k generations at a time; the tick then only
checks whether that step has finished (see Unbounded worlds above).

## wx and GTK pitfalls handled in the code

| Pitfall | Where it is handled |
|---|---|
| wxGTK keeps the C locale, so an implicit `std::string` → `wxString` conversion breaks "×" and "·" | All non-ASCII text goes through `toWx()` (`wxLife/ui/WxConvert.h`) |
| `~wxMenuBar` leaves the frame's pointer to it dangling, so `DestroyChildren()` in a frame destructor deletes the menubar twice | `MainFrame` has no destructor |
| `wxTextCtrl::SetValue` sends an event | `ControlPanel::setRule` uses `ChangeValue` |
| `SetLabel` treats `&` as a mnemonic | Error lines use `SetLabelText` |
| A wheel event passed on with `Skip()` also scrolls GTK's scrolled window | `WorldCanvas::onWheel` never calls `Skip()` |
| A canvas that takes keys can swallow menu accelerators | `onKeyDown` calls `Skip()` when `HasModifiers()` is true (Ctrl or Alt) and for unknown keys |
| GTK's scrolled window around the canvas binds Ctrl+Home, so the Center World accelerator never fires while the canvas has the focus | `onKeyDown` handles Ctrl+Home itself. Text and number boxes keep Ctrl+Home and Ctrl+Delete, because GTK gives them the key before the accelerators. |
| wxGTK turns Ctrl+M into Enter inside a text box, which would apply the rule | `MainFrame`'s `wxEVT_CHAR_HOOK` handler, which runs before any control, sends Max Speed |
| Key-down events report punctuation as on a US layout, and AltGr as Ctrl+Alt | `]`, `[`, `+`, `=` and `-` are matched by character in `WorldCanvas::onChar()`, which accepts AltGr |
| GTK leaves the focus on a clicked button or check box, and Space presses it again | `MainFrame::bindCommands()` gives the focus back to the canvas |
| GTK changes sliders, number boxes and choices under the wheel even when they have no focus | `ControlPanel` consumes those wheel events unless the control has the focus |
| On Enter in a text box, wxGTK presses a dialog's OK button even while it is disabled | `WorldSizeDialog` overrides `Validate()`, which wx asks before it accepts a dialog |
| `wxSpinCtrl::GetValue()` silently clamps what was typed | `WorldSizeDialog` parses `GetTextValue()` |
| The desktop can switch between light and dark while the app runs | `WorldCanvas` and `useErrorColour()` (`wxLife/ui/Theme.h`) handle `wxEVT_SYS_COLOUR_CHANGED` |
| wx asserts when a window is destroyed while it holds the mouse capture | `onClose` calls `cancelStroke()`; `endDrag()` releases the capture only if `HasCapture()` |
| wxGTK sends the second press of a quick double click only as a double-click event | `WorldCanvas` also binds the three `*_DCLICK` events and treats them as presses |
| GTK overlay scrollbars make the client size wx reports differ from the area GTK gives the canvas | `LifeApp::Initialize()` sets `GTK_OVERLAY_SCROLLING=0` before GTK starts |
| A scrollbar range smaller than one page makes GTK (Adwaita) shade the canvas edges | `syncScrollbars()` uses a range of at least one page |
| The client size can change without a size event, for example when the display scale changes | `onPaint` compares sizes, updates the viewport, and uses `CallAfter` for the rest |
| wx's generic status bar repaints synchronously after every text change. Under X11 that paint can come before GTK has moved a slider changed in the same handler, and the slider keeps its old position. | `src/ui/MainFrame.cpp` installs a `StatusBar` subclass whose `DoUpdateStatusText()` only refreshes the field |
| wx blanks the first status field to show menu help | `SetStatusBarPane(-1)` |

## Testing

- **`StepperTest`** compares `BandedStepper` with `ReferenceStepper`, grid for grid, over:
  - sizes from 1 × 1 to 1013 × 777;
  - every preset, B0/S8 and random rules (fewer rules at the two largest sizes);
  - every topology in `kTopologies`;
  - 1, 2, 7 and 64 bands (fewer when the world has fewer rows or cells).

  It also checks known patterns (still lifes, oscillators, gliders) with every engine in `kStepperKinds`.
- **`AntTest`** checks Langton's ant on its own. Turning and stepping are `constexpr`, so their contract
  (left undoes right, four rights are the identity, the clockwise order, and wrapping off each of the
  four edges) is checked at compile time. The four moves that draw a 2 × 2 block and bring the ant back
  are compared with text-art grids.
- **`WorldTest`** covers the automaton in the model: one move per ant per generation, ants sharing a
  grid in index order, wrapping under every topology, and what a switch, Clear, Randomize and a resize
  do to them. `TheAntBuildsTheKnownHighway` is the property test: from an empty 128 × 128 world the ant
  is chaotic for 9977 moves and then repeats the same 104 moves, each period two cells further down the
  diagonal and twelve live cells heavier, with its whole neighbourhood a plain copy of the period
  before. That one test pins the turn rule, the wrapping and the incremental population count together.
- **`RasterizerTest`** compares every pixel with a small independent reference: every cell size from 1
  to 100, and 1500 random scenes, each with up to three ants. The reference gains one branch for them —
  an ant colours the body of its cell but never a grid line — so the overlay, its clipping and both
  paint paths are covered by the same oracle. Below 1 px the reference looks at every cell of a
  pixel's block; a quarter of the random scenes are zoomed out, sparse or dense, and
  `EveryShrinkMatchesThePixelReference` draws a world large enough to be read on several threads.
  Hand-drawn text-art frames cover the special cases, such as blocks cut short by the world's edge.
- **`HashLifeTest`** compares HashLife cell for cell with the dense engine on random soups under seven
  rules, including Seeds and Replicator, which grow at the speed of light, after single and 2^j steps.
  It also checks the textbook facts an unbounded plane gives: the R-pentomino's 116 cells at 1103, the
  acorn's 633 at 5206, the Gosper gun's population after 2^20 generations, and the Primer's first 95
  primes read off its output spaceships. Further tests cover positions near 2^60, the edge of the
  universe, running out of memory and garbage collection, a cancelled step, a step that must leave the
  collection to its owner, and three threads drawing the plane while a step runs.
- **`WorldTest`** also covers the unbounded kind: a grid becoming a plane and back with its pattern and
  generation, rules and edits on a plane, randomizing an area, and far cells that must not wrap into a
  grid. **`ViewportTest`** checks the unbounded view, its reach and zooming out to the whole universe,
  and **`RasterizerTest`** checks that a plane looks pixel for pixel like a grid with the same cells,
  at scales from 1/8 to 16 px.
- **`PatternTest`** reads RLE and plaintext text, including the tolerances real files need (CRLF, runs
  split by white space and line ends, a missing `!`, every rule spelling), and every error with its line.
  **`PatternSetupTest`** covers the demo and file setups, including the margins shrinking to the budget.
  **`DemoTest`** checks every demo against its embedded file and world, and runs the Gosper gun, the
  first primes of the Primer and the ant demos. **`ThumbnailTest`** compares previews with text art.
- **`ViewportTest`** checks anchored zoom for every pair of zoom steps and every anchor position inside
  a cell against a floating-point reference, checks that zooming there and back restores the offset,
  below 1 px and across it too, and compares `cellAt` and `visibleCells` with brute-force results, below
  1 px pixel by pixel against the blocks they show.
- **Other suites.** The rest of `core` (worlds, rules, grids, lines, bands, random numbers, limits,
  speed, pacing and the rate meter, formatting) has its own suites. `tests/support/AsciiGrid.h` lets
  tests write patterns as text, such as `".O."`.
- **`GuiSmokeTest`** (CTest label `gui`, in `tests/ui/GuiSmokeTests.cpp`) opens a real `MainFrame` per
  test. It is built only with wxGTK, so only on Linux. It sends commands with
  `emitCommand()` and synthetic wx events to the controls, the canvas and the scrollbars, types into
  number boxes through GTK, and answers dialogs with a `wxModalDialogHook`. wx starts in the suite's
  `SetUpTestSuite()`, so only the processes that run these tests start GTK. The tests run one at a time.
  They are skipped when no display is configured, and they fail when a configured display cannot be
  opened. `RunsUnboundedWorlds` turns the world into a plane in the size dialog, opens a glider into
  it, steps it 2^10 and runs it at 2^20 generations a step, draws, randomizes, is refused a B0 rule,
  and goes back to a fixed size. `ZoomsOutBelowOnePixel` fits a world larger than the canvas, checks
  the panel and the status bar, pans with a drag, and zooms a plane out to the whole universe.
- **Checks outside GoogleTest.** CTest also runs `layering` and `static_link` (`ldd`, `otool -L` or
  `dumpbin /dependents` must list no wxWidgets library for `wxLife`). Configuring already fails if `wx::core` or `wx::base` is not the static library
  built from the fetched sources.
- **Presets.** The `headless` preset builds the kernel without clones, so its tests cover the baseline
  loop; the other presets use the AVX2 clone on CPUs that have AVX2. The `asan` preset runs every test,
  with leak detection off for the GUI tests (GTK leaks at exit by design), and UBSan stops at the first
  error (`-fno-sanitize-recover=all`). The `tsan` preset has no UI. Under both sanitizers,
  `WorldTest.FailedResizeLeavesTheWorldUnchanged` reports itself as skipped: their allocators abort
  instead of throwing `std::bad_alloc`.
- **Manual checks.** The GUI smoke tests bypass GTK's own key and mouse handling, so the README keeps a
  manual smoke checklist for those paths.

## Extension points

| Future feature | What already exists | What would change |
|---|---|---|
| More B/S presets | `kRulePresets` | One line per preset. The parser is `constexpr`, so a typo fails to compile. |
| New engines (bit-packed rows, explicit SIMD, skipping empty rows) | `Stepper`, `StepperKind`, `kStepperKinds`, `makeStepper()`, `kernel::stepRow()`, `wxLife_bench --engine` | Add a class, an enum value, a `kStepperKinds` entry and a `makeStepper()` case. `-Wswitch` then points at the other switches that need a case: `toString(StepperKind)`, the benchmark's `bandCount()` and `engineMenuItem()` in `src/ui/MainFrame.cpp`. The pattern tests in `StepperTest` pick the engine up from `kStepperKinds`; add it to the comparison with `ReferenceStepper` there. In the UI: a `CommandId`, a radio item in `buildMenuBar()` and a handler row in `bindCommands()`. |
| Persistent thread pool | `forEachBand()` is the only code that creates threads | Replace its body; nothing else changes. A quick prototype pool stepped 1000² in 0.15 ms with 2 bands (0.38 ms with threads started per step) and in 0.09 ms with 4, so `suggestedBandCount()` could then split smaller worlds too. |
| Background stepping for fixed-size worlds (more than about 200 million cells on this machine) | `SimulationRunner` already steps planes on a worker thread (`launchStep()`, `finishStep()`, `cancelStep()`); painting a grid reads only `World::cells()` | Step into the spare grid on the worker and swap on the UI thread when it is done. A grid step cannot be undone halfway, so cancelling would have to wait for it or step a copy. |
| Other rule families (Generations, Larger than Life) | Only the steppers interpret a `Rule`; the rest of the code only parses, prints and compares it. `Cell` is a byte. | Make `Rule` a `std::variant` and give each family its own stepper, plus a case in `Rule::toString()`, `findPreset()` and the preset list. The rasterizer would need colours for the extra states. |
| More automata (other turmites, multi-state ants) | `Automaton`, `kAutomata` and the `default`-less switch in `World::step()`; `wxLife/core/Ant.h` holds the ant's own rule | Add an enum value and a `kAutomata` entry; `-Wswitch` then points at the four switches that need a case: `toString(Automaton)`, `World::step()`, and `worldText()` and `automatonMenuItem()` in `src/ui/MainFrame.cpp`. The panel's choice is built from `kAutomata`, so it needs no change. Multi-state cells would additionally break the binary assumptions listed in the row above. With a third automaton it is time to extract an interface from `World` instead of widening the switch. |
| Other topologies (cylinder, Klein bottle) | `Topology` and `kTopologies`. `-Wswitch` lists the `core` code that needs a case: `toString(Topology)`, `Grid::updateBorder()` and the `alive` lambda in `ReferenceStepper::step()`. | Add an enum value, a `kTopologies` entry and a copy rule; `StepperTest` and `WorldTest` then cover it. The Wrap Edges toggle in `MainFrame` would become a choice. |
| Macrocell files, giant demos | Unbounded worlds; `readPattern()`; `PatternSetup::kind` | A macrocell reader that builds HashLife nodes directly, and demos with `kind = UNBOUNDED`. That brings the patterns that do not fit a dense world, such as the Caterpillar (4,195 × 330,721 cells) and Gemini, and lets the prime calculators run without their streams hitting an edge. |
| A third kind of world, or HashLife for more automata | `WorldKind`, the `kind()` branches in `World`, `WorldCanvas` and `MainFrame` | With a third kind it is time to extract an interface from `World` with one implementation per kind, rather than more branches. |
| A minimap | `Viewport` and `Rasterizer` draw any part of any world at any scale, down to all of it (`maxShrink()`) | A second small canvas with its own `Viewport` at `maxShrink()`, drawing the main view's `visibleCells()` as a frame over it, and turning clicks into `centerOn()`. |
| More demos | `patterns/`, `demo_patterns` in `src/CMakeLists.txt`, `kDemos` in `src/core/Demo.cpp` | Add the file, list it, and add a `Demo`; `DemoTest` checks the rest. `patterns/README.md` has the steps. |
| Saving patterns, more file formats | `readPattern()`, `MainFrame::openPatternFile()` | An RLE writer next to the reader and File → Save. Life 1.06 would be a third reader behind the same format check. |
| Undo/redo, selection | Every edit enters through a `CommandId` or through `paintCells` → `MainFrame::onPaintCells()` | Let `World::setCells()` report the changed cells, and add an undo stack in `ui/`. |
| Themes, saved settings | `RenderStyle`, `darkStyle()`/`lightStyle()`, `ui::defaults` and `Speed` are plain values. Only `wxLife/ui/Theme.h` (`themeStyle()`, `useErrorColour()`) picks colours from the desktop theme. | Load and save them with `wxConfig` in `LifeApp`. A user theme would replace the choice in `wxLife/ui/Theme.h`. |
| Parallel rasterizer, OpenGL canvas | `Rasterizer::render()` (pixel rows are independent); `WorldCanvas::onPaint()` is the only blit | Use `forEachBand()` over pixel rows, or add a `wxGLCanvas` variant (turn `wxUSE_OPENGL` back on). |
| Statistics, history plots | `MainFrame::onSimulationTick()` receives every `TickReport` | Subscribe there. |
| Command-line batch runs | `wxLife_bench` links only `wxLife_lib` | Add a `wxLife_cli` next to it. |

## Suggested reading order

Headers are in `include/wxLife/`, sources in `src/`.

1. `core/Rule.h`: the B/S masks, the `constexpr` parser and the presets.
2. `core/Grid.h`, `core/Grid.cpp`: the ghost border and the torus copy order.
3. `core/ReferenceStepper.cpp`: the executable specification.
4. `core/StepKernel.cpp`: the only hot loop.
5. `core/ParallelBands.h`, `core/BandedStepper.cpp`: the parallel bands.
6. `core/World.h`, `core/World.cpp`: the model, its counters and its edits.
7. `core/Ant.h`: Langton's ant, and with it the other half of `World::step()`.
8. `core/Pacer.h`, `core/Pacer.cpp`: turning wall time into generations.
9. `render/Viewport.h`, `render/Viewport.cpp`: the camera, clamping and anchored zoom.
10. `render/Rasterizer.cpp`: cell stamps and row copying.
11. `ui/SimulationRunner.cpp`: the one-shot timer, and the worker thread that steps planes.
12. `ui/WorldCanvas.cpp`: painting, scrollbars, mouse and keys.
13. `ui/MainFrame.cpp`: the command table, syncing the controls, and the status bar.
14. `core/Pattern.cpp`, `core/Demo.cpp`, `core/PatternSetup.cpp`: reading pattern files, the demo
    catalogue, and what loading one does; then `MainFrame::loadPattern()`.
15. `core/HashLife.h`, `core/HashLife.cpp`: the quadtree, `successor()`, and the rules that let one
    thread step while others draw.
