#include "wxLife/core/World.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/BandedStepper.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Random.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{

namespace
{

// Every row has its own generator, seeded from the row number, so the band split cannot change the
// result. The row seed is hashed first. SplitMix64 advances its state by the same constant, so for
// small seeds the unhashed row seeds lie on one shared sequence and the rows come out as shifted
// copies of each other.
void randomizeRow(std::span<Cell> cells, Coord y, std::uint64_t seed, unsigned threshold) noexcept
{
    const std::uint64_t rowSeed =
        seed ^ (0x9E3779B97F4A7C15U * (static_cast<std::uint64_t>(y) + 1));
    const std::uint64_t hashedSeed = SplitMix64(rowSeed)();
    SplitMix64          random(hashedSeed);
    std::uint64_t       bytes = 0;
    for (std::size_t x = 0; x < cells.size(); ++x)
    {
        if (x % 8 == 0)
        {
            bytes = random();  // eight cells per number, lowest byte first
        }
        cells[x] = (bytes & 0xFFU) < threshold ? kAlive : kDead;
        bytes >>= 8;
    }
}

}  // namespace

World::World(Extent extent, Rule rule, Topology topology)
  : m_current(extent),
    m_next(extent),
    m_rule(rule),
    m_topology(topology),
    m_stepper(std::make_unique<BandedStepper>())
{
    // Room for every ant up front, so adding one later never reallocates and toggleAntAt() can be
    // noexcept.
    m_ants.reserve(static_cast<std::size_t>(kMaxAnts));
}

WorldKind World::kind() const noexcept
{
    return m_plane ? WorldKind::UNBOUNDED : WorldKind::FIXED_SIZE;
}

Extent World::extent() const noexcept
{
    return m_current.extent();
}

const Grid& World::cells() const noexcept
{
    return m_current;
}

const HashLife& World::plane() const noexcept
{
    assert(m_plane);
    return *m_plane;
}

Cell World::at(CellPos p) const noexcept
{
    return m_current.at(p);
}

Cell World::cellAt(UniversePos p) const noexcept
{
    if (m_plane)
    {
        return m_plane->at(p);
    }
    const Extent e = extent();
    if (p.x < 0 || p.y < 0 || p.x >= e.width || p.y >= e.height)
    {
        return kDead;
    }
    return m_current.at({.x = static_cast<Coord>(p.x), .y = static_cast<Coord>(p.y)});
}

const Rule& World::rule() const noexcept
{
    return m_rule;
}

Topology World::topology() const noexcept
{
    return m_topology;
}

Automaton World::automaton() const noexcept
{
    return m_automaton;
}

std::span<const Ant> World::ants() const noexcept
{
    return m_ants;
}

const Stepper& World::stepper() const noexcept
{
    return *m_stepper;
}

std::uint64_t World::generation() const noexcept
{
    return m_plane ? m_generation + m_plane->generation() : m_generation;
}

CellCount World::population() const noexcept
{
    return m_plane ? static_cast<CellCount>(m_plane->population()) : m_population;
}

void World::step()
{
    if (m_plane)
    {
        const std::expected<void, HashLifeError> stepped = m_plane->step(0);
        if (!stepped && stepped.error() == HashLifeError::UNIVERSE_EDGE)
        {
            throw std::length_error(std::string(describe(stepped.error())));
        }
        if (!stepped)
        {
            throw std::bad_alloc();  // HashLife collects and retries before it gives up
        }
        return;
    }
    switch (m_automaton)
    {
        case Automaton::LIFE:
            m_current.updateBorder(m_topology);
            m_population = m_stepper->step(m_current, m_next, m_rule, m_topology);
            std::swap(m_current, m_next);  // swaps the buffers, so no grid is allocated per step
            break;
        case Automaton::LANGTON_ANT:
            // One move each, in index order over the one grid, so an ant sees what the ones before
            // it left.
            for (Ant& ant : m_ants)
            {
                m_population += advance(ant, m_current);
            }
            break;
    }
    ++m_generation;
}

std::expected<void, HashLifeError> World::stepPlane(unsigned                   exponent,
                                                    const HashLifeStepOptions& options)
{
    assert(m_plane);
    return m_plane->step(exponent, options);
}

void World::collectIfFull()
{
    if (m_plane)
    {
        m_plane->collectIfFull();
    }
}

void World::collectGarbage()
{
    if (m_plane)
    {
        m_plane->collectGarbage();
    }
}

CellCount World::setCells(std::span<const CellPos> cells, Cell value, CellPos offset)
{
    assert(value == kDead || value == kAlive);
    if (m_plane)
    {
        return m_plane->setCells(cells, value, {.x = offset.x, .y = offset.y});
    }
    const Extent bounds  = extent();
    CellCount    changed = 0;
    for (const CellPos cell : cells)
    {
        // 64-bit, so a far-away position cannot wrap around into the world.
        const std::int64_t x = std::int64_t{cell.x} + offset.x;
        const std::int64_t y = std::int64_t{cell.y} + offset.y;
        if (x < 0 || y < 0 || x >= bounds.width || y >= bounds.height)
        {
            continue;
        }
        const CellPos p{.x = static_cast<Coord>(x), .y = static_cast<Coord>(y)};
        if (m_current.at(p) != value)
        {
            m_current.set(p, value);
            ++changed;
        }
    }
    m_population += value == kAlive ? changed : -changed;
    return changed;
}

CellCount World::setCells(std::span<const UniversePos> cells, Cell value)
{
    assert(value == kDead || value == kAlive);
    if (m_plane)
    {
        return m_plane->setCells(cells, value);
    }
    const Extent bounds  = extent();
    CellCount    changed = 0;
    for (const UniversePos cell : cells)
    {
        // Checked in 64 bits, so a far-away cell cannot wrap around into the grid.
        if (cell.x < 0 || cell.y < 0 || cell.x >= bounds.width || cell.y >= bounds.height)
        {
            continue;
        }
        const CellPos p{.x = static_cast<Coord>(cell.x), .y = static_cast<Coord>(cell.y)};
        if (m_current.at(p) != value)
        {
            m_current.set(p, value);
            ++changed;
        }
    }
    m_population += value == kAlive ? changed : -changed;
    return changed;
}

bool World::setCell(CellPos p, Cell value)
{
    return setCells({&p, 1}, value) == 1;
}

void World::clear()
{
    if (m_plane)
    {
        m_plane->clear();
        m_generation = 0;
        return;
    }
    m_current.clear();  // m_next is overwritten by the next step anyway
    layOutAnts();       // generation 0 means the ants are back on their starting spots too
    m_generation = 0;
    m_population = 0;
}

void World::randomize(double density, std::uint64_t seed)
{
    assert(!m_plane);
    // A cell is alive when a random byte (0..255) is below the threshold: 0 keeps every cell dead,
    // 256 makes every cell alive.
    const auto threshold =
        static_cast<unsigned>(std::lround(std::clamp(density, 0.0, 1.0) * 256.0));
    const unsigned bands = suggestedBandCount(extent().cellCount());
    forEachBand(extent().height, bands, [&](unsigned, Coord firstRow, Coord endRow) {
        for (Coord y = firstRow; y < endRow; ++y)
        {
            randomizeRow(m_current.row(y), y, seed, threshold);
        }
    });
    m_population = m_current.countAlive();
    layOutAnts();
    m_generation = 0;
}

void World::randomize(double density, std::uint64_t seed, UniverseRect area)
{
    assert(m_plane);
    assert(area.x1 - area.x0 <= kMaxWorldSide && area.y1 - area.y0 <= kMaxWorldSide);
    const auto threshold =
        static_cast<unsigned>(std::lround(std::clamp(density, 0.0, 1.0) * 256.0));
    std::vector<Cell> row(static_cast<std::size_t>(std::max<UniverseCoord>(area.x1 - area.x0, 0)));
    std::vector<UniversePos> alive;
    for (UniverseCoord y = area.y0; y < area.y1; ++y)
    {
        randomizeRow(row, static_cast<Coord>(y - area.y0), seed, threshold);
        for (std::size_t x = 0; x < row.size(); ++x)
        {
            if (row[x] == kAlive)
            {
                alive.push_back({.x = area.x0 + static_cast<UniverseCoord>(x), .y = y});
            }
        }
    }
    m_plane->clear();
    m_plane->setCells(alive, kAlive);
    m_generation = 0;
}

void World::resize(Extent newExtent, bool keepPattern)
{
    // Allocate before changing anything: if either allocation throws, the world is untouched.
    Grid current(newExtent);
    Grid next(newExtent);
    if (m_plane)
    {
        // From an unbounded plane: the cells around (0, 0) become the grid, that cell its centre.
        const UniversePos corner{.x = -(newExtent.width / 2), .y = -(newExtent.height / 2)};
        if (keepPattern)
        {
            m_plane->forEachBlock({.x0 = corner.x,
                                   .y0 = corner.y,
                                   .x1 = corner.x + newExtent.width,
                                   .y1 = corner.y + newExtent.height},
                                  0, [&](UniversePos cell) {
                                      current.set({.x = static_cast<Coord>(cell.x - corner.x),
                                                   .y = static_cast<Coord>(cell.y - corner.y)},
                                                  kAlive);
                                  });
        }
        m_generation = keepPattern ? generation() : 0;
        m_current    = std::move(current);
        m_next       = std::move(next);
        m_population = m_current.countAlive();
        m_plane.reset();
        layOutAnts();  // the ants were nowhere while the world was unbounded
        return;
    }

    // Offset of the old grid inside the new one; negative when shrinking. Division truncates toward
    // zero, so growing and then shrinking back restores the original position.
    const Extent old = extent();
    const Coord  dx  = (newExtent.width - old.width) / 2;
    const Coord  dy  = (newExtent.height - old.height) / 2;

    if (keepPattern)
    {
        // The overlap, in old coordinates.
        const Coord x0    = std::max(0, -dx);
        const Coord x1    = std::min(old.width, newExtent.width - dx);
        const Coord y0    = std::max(0, -dy);
        const Coord y1    = std::min(old.height, newExtent.height - dy);
        const auto  index = [](Coord c) { return static_cast<std::size_t>(c); };
        for (Coord y = y0; y < y1; ++y)
        {
            const std::span<const Cell> from = m_current.row(y).subspan(index(x0), index(x1 - x0));
            std::ranges::copy(from, current.row(y + dy).subspan(index(x0 + dx)).begin());
        }
    }
    else
    {
        m_generation = 0;
    }

    m_current    = std::move(current);
    m_next       = std::move(next);
    m_population = m_current.countAlive();

    if (keepPattern)
    {
        // The ants travel with the pattern; one the new world cropped comes back to the nearest
        // edge.
        for (Ant& ant : m_ants)
        {
            ant.position.x = std::clamp(ant.position.x + dx, 0, newExtent.width - 1);
            ant.position.y = std::clamp(ant.position.y + dy, 0, newExtent.height - 1);
        }
    }
    else
    {
        layOutAnts();
    }
}

void World::makeUnbounded(bool keepPattern, std::uint64_t memoryBudgetBytes)
{
    assert(HashLife::supports(m_rule) && m_automaton == Automaton::LIFE);
    if (m_plane)
    {
        if (!keepPattern)
        {
            clear();
        }
        return;
    }
    // Build everything first, so a failed allocation leaves the world as it was.
    auto plane = std::make_unique<HashLife>(m_rule, memoryBudgetBytes);
    if (keepPattern)
    {
        const Extent             e = extent();
        std::vector<UniversePos> alive;
        for (Coord y = 0; y < e.height; ++y)
        {
            const std::span<const Cell> row = m_current.row(y);
            for (Coord x = 0; x < e.width; ++x)
            {
                if (row[static_cast<std::size_t>(x)] == kAlive)
                {
                    alive.push_back({.x = x - (e.width / 2), .y = y - (e.height / 2)});
                }
            }
        }
        plane->setCells(alive, kAlive);
    }
    Grid emptyCurrent;
    Grid emptyNext;
    m_generation = keepPattern ? m_generation : 0;
    m_current    = std::move(emptyCurrent);
    m_next       = std::move(emptyNext);
    m_population = 0;
    m_plane      = std::move(plane);
}

void World::setRule(const Rule& rule)
{
    m_rule = rule;
    if (m_plane)
    {
        m_plane->setRule(rule);
    }
}

void World::setTopology(Topology topology) noexcept
{
    m_topology = topology;
}

void World::setAutomaton(Automaton automaton)
{
    assert(!m_plane || automaton == Automaton::LIFE);
    m_automaton = automaton;
    if (m_automaton == Automaton::LANGTON_ANT && m_ants.empty())
    {
        resetAnts(1);
    }
}

void World::setAnts(std::span<const Ant> ants)
{
    m_ants.clear();
    for (const Ant& ant : ants)
    {
        if (m_ants.size() >= static_cast<std::size_t>(kMaxAnts))
        {
            break;
        }
        if (extent().contains(ant.position))
        {
            m_ants.push_back(ant);
        }
    }
}

void World::resetAnts(int count)
{
    m_ants.resize(static_cast<std::size_t>(std::clamp(count, 0, kMaxAnts)));
    layOutAnts();
}

bool World::toggleAntAt(CellPos p) noexcept
{
    if (!extent().contains(p))
    {
        return false;
    }
    if (std::erase_if(m_ants, [p](const Ant& ant) { return ant.position == p; }) > 0)
    {
        return false;
    }
    if (m_ants.size() >= static_cast<std::size_t>(kMaxAnts))
    {
        return false;
    }
    m_ants.push_back({.position = p, .heading = Heading::NORTH});  // within the reserved capacity
    return true;
}

void World::layOutAnts() noexcept
{
    const auto count = static_cast<int>(m_ants.size());
    for (int i = 0; i < count; ++i)
    {
        m_ants[static_cast<std::size_t>(i)] = defaultAnt(i, count, extent());
    }
}

void World::setStepper(std::unique_ptr<Stepper> stepper) noexcept
{
    assert(stepper != nullptr);
    m_stepper = std::move(stepper);
}

}  // namespace wxLife::core
