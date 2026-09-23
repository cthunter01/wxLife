#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// The simulation model: current generation, automaton, rule, topology, stepping engine and
/// counters. A world is either a grid of fixed size or an unbounded plane (WorldKind); the plane is
/// a HashLife, and the Life rule is the only automaton it runs.
/// @note Not thread-safe; the UI thread owns it. step() and randomize() use worker threads
///       internally and return only after they have finished. stepPlane() may run on a thread of
///       its own; while it does, other threads may only read: kind(), plane() and its reading
///       functions, generation() and population().
/// @note Every ant is always inside the world: setAnts(), resetAnts(), toggleAntAt() and
///       resize() are the four places that have to keep it that way.
class World
{
public:
    /// Uses a BandedStepper. @pre validateExtent(extent, ...) succeeded. @throws std::bad_alloc
    explicit World(Extent extent, Rule rule = {}, Topology topology = Topology::TORUS);

    [[nodiscard]] WorldKind kind() const noexcept;
    /// {} for an unbounded world.
    [[nodiscard]] Extent extent() const noexcept;
    /// The grid of a fixed-size world; an empty grid for an unbounded one.
    [[nodiscard]] const Grid& cells() const noexcept;
    /// @pre kind() == WorldKind::UNBOUNDED
    [[nodiscard]] const HashLife& plane() const noexcept;
    /// @pre extent().contains(p), in a fixed-size world
    [[nodiscard]] Cell at(CellPos p) const noexcept;
    /// Any cell of any world; dead outside a fixed-size one.
    [[nodiscard]] Cell        cellAt(UniversePos p) const noexcept;
    [[nodiscard]] const Rule& rule() const noexcept;
    [[nodiscard]] Topology    topology() const noexcept;
    [[nodiscard]] Automaton   automaton() const noexcept;
    /// Every ant is inside the world.
    [[nodiscard]] std::span<const Ant> ants() const noexcept;
    [[nodiscard]] const Stepper&       stepper() const noexcept;
    [[nodiscard]] std::uint64_t        generation() const noexcept;
    /// Kept up to date incrementally.
    [[nodiscard]] CellCount population() const noexcept;

    /// Advances one generation: a whole Life step, or one move for each ant in turn. An unbounded
    /// world steps with HashLife and collects its garbage when it needs to.
    /// @throws std::bad_alloc when an unbounded world runs out of memory, std::length_error when
    ///         its pattern reaches the edge of the universe; nothing changes then
    void step();
    /// Advances an unbounded world 2^exponent generations; see HashLife::step(). This is the call
    /// that may run on another thread. @pre kind() == WorldKind::UNBOUNDED
    std::expected<void, HashLifeError> stepPlane(unsigned                   exponent,
                                                 const HashLifeStepOptions& options = {});
    /// An unbounded world's garbage collection (HashLife::collectIfFull()); for a fixed-size world
    /// it does nothing. Call it between steps.
    void collectIfFull();
    /// The same, however full the plane is (HashLife::collectGarbage()).
    void collectGarbage();

    /// Sets every listed cell, moved by `offset`; positions outside the world are ignored.
    /// generation is unchanged. The offset places a pattern read from a file.
    /// @pre value is kDead or kAlive
    /// @return number of cells that actually changed.
    /// @throws std::bad_alloc when an unbounded world runs out of memory; nothing changes then
    CellCount setCells(std::span<const CellPos> cells, Cell value, CellPos offset = {});
    /// The same with 64-bit positions, as the view of an unbounded world gives them.
    CellCount setCells(std::span<const UniversePos> cells, Cell value);
    /// @return true if the cell changed.
    bool setCell(CellPos p, Cell value);
    /// All dead; generation = 0; the ants start over.
    void clear();
    /// Each cell becomes alive with probability `density` (clamped to [0, 1], resolution 1/256).
    /// The result depends only on (seed, extent, density), never on the thread count.
    /// generation = 0, and the ants start over: generation 0 means they are back on their
    /// starting spots.
    void randomize(double density, std::uint64_t seed);
    /// An unbounded world's version: clears the plane, then fills `area` as randomize() fills a
    /// fixed-size world, the same way for the same (seed, area, density).
    /// @pre kind() == WorldKind::UNBOUNDED; area smaller than kMaxWorldSide on each side
    void randomize(double density, std::uint64_t seed, UniverseRect area);
    /// Changes the size. keepPattern keeps the overlapping region centred, the generation and the
    /// ants, which move with the pattern and are pulled back inside a world that cropped them;
    /// otherwise the world is cleared, generation = 0 and the ants start over. An unbounded world
    /// becomes a fixed-size one; with keepPattern, the cells around the plane's (0, 0) are kept,
    /// that cell becoming the grid's centre.
    /// Strong exception guarantee. @throws std::bad_alloc
    /// @note The old and the new grids exist at the same time, so the peak memory is
    ///       worldBytes(old) + worldBytes(new).
    void resize(Extent newExtent, bool keepPattern);
    /// Turns the world into an unbounded plane, or clears the one it is. keepPattern keeps the
    /// cells, the grid's centre cell landing at (0, 0), and the generation. The grids' memory is
    /// released. Strong exception guarantee.
    /// @pre HashLife::supports(rule()) and automaton() == Automaton::LIFE
    /// @throws std::bad_alloc
    void makeUnbounded(bool keepPattern, std::uint64_t memoryBudgetBytes);
    /// @pre an unbounded world only takes rules that HashLife::supports()
    void setRule(const Rule& rule);
    void setTopology(Topology topology) noexcept;
    /// Switching to LangtonAnt with no ants yet puts one in the middle. Switching to Life keeps the
    /// ants, so going back and forth loses nothing.
    /// @pre an unbounded world only runs Automaton::LIFE
    void setAutomaton(Automaton automaton);
    /// Ants outside the world are dropped; at most kMaxAnts.
    void setAnts(std::span<const Ant> ants);
    /// `count` ants (clamped to [0, kMaxAnts]) on their spots.
    void resetAnts(int count);
    /// Adds an ant facing north, or removes every ant already standing there.
    /// Outside the world: no-op.
    /// @return true if an ant was added, false otherwise.
    bool toggleAntAt(CellPos p) noexcept;
    /// @pre stepper != nullptr
    void setStepper(std::unique_ptr<Stepper> stepper) noexcept;

private:
    /// Puts the ants back on their starting spots, keeping how many there are. Never allocates.
    void layOutAnts() noexcept;

    Grid                      m_current;
    Grid                      m_next;  ///< Only Life steps into it; the ants use m_current alone.
    Rule                      m_rule;
    Topology                  m_topology;
    Automaton                 m_automaton = Automaton::LIFE;
    std::vector<Ant>          m_ants;
    std::unique_ptr<Stepper>  m_stepper;
    std::uint64_t             m_generation = 0;  ///< Of a fixed-size world, or the plane's start
    std::unique_ptr<HashLife> m_plane;           ///< Only while the world is unbounded
    CellCount                 m_population = 0;
};

}  // namespace wxLife::core
