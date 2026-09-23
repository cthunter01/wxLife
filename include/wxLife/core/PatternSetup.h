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
/// pattern's cells are set at `origin`, and it starts at generation 0. A macrocell pattern instead
/// becomes the plane, as its file places it and at its generation.
struct PatternSetup
{
    WorldKind kind = WorldKind::FIXED_SIZE;
    Extent    world;   ///< For a fixed-size world
    CellPos   origin;  ///< Where the pattern's top-left corner goes; not for a macrocell.
    Topology  topology = Topology::BOUNDED;
    Rule      rule;
    Automaton automaton = Automaton::LIFE;
    /// The ants, for Automaton::LANGTON_ANT. Life leaves the world's ants alone.
    std::vector<Ant>     ants;
    std::optional<Speed> speed;  ///< nullopt keeps the current speed.
    /// On a plane: generations per step, 2^exponent; nullopt keeps the current step size.
    std::optional<unsigned> stepExponent;
    /// The cells to show first; nullopt fits the world, or on a plane the pattern.
    std::optional<UniverseRect> view;
};

/// Cells of empty space a pattern file gets on each side: half its own size, but at least this.
inline constexpr Coord kMinFileMargin = 50;

/// The demo's own settings. The rule is the one its file names, or Conway's Life.
/// @pre pattern is the demo's pattern; it fits the demo's world at the demo's origin, and a
///      macrocell comes only with an unbounded demo.
[[nodiscard]] PatternSetup demoSetup(const Demo& demo, const Pattern& pattern);

/// A world for a pattern read from a file. In a fixed-size world: the pattern in the middle of a
/// new one, with kMinFileMargin or half its size of empty space around it, whichever is more, but
/// no more than the side limit and the memory budget allow. In an unbounded world: the plane,
/// cleared, with the pattern centred on (0, 0). A macrocell always gets an unbounded world, where
/// it stays as its file places it. The rule is the file's, or `currentRule` if it names none; the
/// kind (but for a macrocell), the topology, the speed and the step size stay as they are.
/// @return why not even the pattern itself fits a fixed-size world.
[[nodiscard]] std::expected<PatternSetup, ExtentError> fileSetup(const Pattern& pattern,
                                                                 const Rule&    currentRule,
                                                                 WorldKind      currentKind,
                                                                 Topology       currentTopology,
                                                                 std::uint64_t  memoryBudgetBytes);

}  // namespace wxLife::core
