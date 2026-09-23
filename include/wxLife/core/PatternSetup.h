/// @file
/// What loading a pattern does to the world: its size, edges, rule and the rest.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Demo.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{

/// Everything loading a pattern sets up. The world is replaced: it is resized and cleared, then the
/// pattern's cells are set at `origin`, and it starts at generation 0.
struct PatternSetup
{
    Extent    world;
    CellPos   origin;  ///< Where the pattern's top-left corner goes.
    Topology  topology = Topology::BOUNDED;
    Rule      rule;
    Automaton automaton = Automaton::LIFE;
    /// The ants, for Automaton::LANGTON_ANT. Life leaves the world's ants alone.
    std::vector<Ant>        ants;
    std::optional<Speed>    speed;  ///< nullopt keeps the current speed.
    std::optional<CellRect> view;   ///< The cells to show first; nullopt fits the world.
};

/// Cells of empty space a pattern file gets on each side: half its own size, but at least this.
inline constexpr Coord kMinFileMargin = 50;

/// The demo's own settings. The rule is the one its file names, or Conway's Life.
/// @pre pattern is the demo's pattern; it fits the demo's world at the demo's origin.
[[nodiscard]] PatternSetup demoSetup(const Demo& demo, const Pattern& pattern);

/// A world for a pattern read from a file: the pattern in the middle, with kMinFileMargin or half
/// its size of empty space around it, whichever is more, but no more than the side limit and the
/// memory budget allow. The rule is the file's, or `currentRule` if it names none; the topology
/// and the speed stay as they are.
/// @return why not even the pattern itself fits.
[[nodiscard]] std::expected<PatternSetup, ExtentError> fileSetup(const Pattern& pattern,
                                                                 const Rule&    currentRule,
                                                                 Topology       currentTopology,
                                                                 std::uint64_t  memoryBudgetBytes);

}  // namespace wxLife::core
