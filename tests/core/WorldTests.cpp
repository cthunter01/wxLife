#include "wxLife/core/World.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "support/AsciiGrid.h"
#include "wxLife/core/Ant.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Random.h"
#include "wxLife/core/ReferenceStepper.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

using test::gridFromAscii;
using test::toAscii;

using Pattern = std::initializer_list<std::string_view>;

// Sets the live cells of `pattern` with its top-left cell at `at`.
void draw(World& world, Pattern pattern, CellPos at = {.x = 0, .y = 0})
{
    const Grid source = gridFromAscii(pattern);
    for (Coord y = 0; y < source.extent().height; ++y)
    {
        for (Coord x = 0; x < source.extent().width; ++x)
        {
            if (source.at({.x = x, .y = y}) == kAlive)
            {
                world.setCell({.x = at.x + x, .y = at.y + y}, kAlive);
            }
        }
    }
}

const Pattern kGlider = {
    ".O.",
    "..O",
    "OOO",
};

TEST(WorldTest, StartsEmptyWithTheGivenSettings)
{
    const World world({.width = 7, .height = 5}, Rule::parse("B36/S23").value(), Topology::BOUNDED);
    EXPECT_EQ(world.extent(), (Extent{7, 5}));
    EXPECT_EQ(world.cells().extent(), (Extent{7, 5}));
    EXPECT_EQ(world.rule().toString(), "B36/S23");
    EXPECT_EQ(world.topology(), Topology::BOUNDED);
    EXPECT_EQ(world.stepper().kind(), StepperKind::BANDED);
    EXPECT_EQ(world.generation(), 0U);
    EXPECT_EQ(world.population(), 0);

    const World defaults({.width = 1, .height = 1});
    EXPECT_EQ(defaults.rule().toString(), "B3/S23");
    EXPECT_EQ(defaults.topology(), Topology::TORUS);
}

TEST(WorldTest, SetCellsMovesTheCellsByTheOffset)
{
    World                      world({.width = 5, .height = 4});
    const std::vector<CellPos> glider{
        {.x = 1, .y = 0}, {.x = 2, .y = 1}, {.x = 0, .y = 2}, {.x = 1, .y = 2}, {.x = 2, .y = 2}};
    EXPECT_EQ(world.setCells(glider, kAlive, {.x = 2, .y = 1}), 5);
    EXPECT_EQ(toAscii(world.cells()),
              ".....\n"
              "...O.\n"
              "....O\n"
              "..OOO\n");
    EXPECT_EQ(world.population(), 5);

    // Cells that land outside are left out, also for offsets far enough to overflow 32 bits.
    world.clear();
    EXPECT_EQ(world.setCells(glider, kAlive, {.x = 3, .y = 2}), 1);  // only (1, 0) lands inside
    EXPECT_EQ(world.at({.x = 4, .y = 2}), kAlive);
    constexpr Coord kFar = std::numeric_limits<Coord>::max();
    EXPECT_EQ(world.setCells(glider, kAlive, {.x = kFar, .y = kFar}), 0);
    EXPECT_EQ(world.setCells(glider, kAlive, {.x = -kFar, .y = 0}), 0);
    EXPECT_EQ(world.population(), 1);
}

TEST(WorldTest, GliderOnATorusReturnsHome)
{
    World world({.width = 8, .height = 8});
    draw(world, kGlider, {.x = 2, .y = 3});
    const Grid start = world.cells();
    for (int g = 0; g < 32; ++g)
    {
        world.step();
    }
    EXPECT_EQ(world.cells(), start);
    EXPECT_EQ(world.generation(), 32U);
    EXPECT_EQ(world.population(), 5);
}

TEST(WorldTest, OnlyStepAdvancesTheGeneration)
{
    World world({.width = 10, .height = 10});
    world.step();
    world.step();
    EXPECT_EQ(world.generation(), 2U);

    world.setCell({.x = 1, .y = 1}, kAlive);
    world.setRule(Rule::parse("B36/S23").value());
    world.setTopology(Topology::BOUNDED);
    world.setStepper(makeStepper(StepperKind::REFERENCE));
    world.resize({.width = 12, .height = 9}, true);
    EXPECT_EQ(world.generation(), 2U);
}

TEST(WorldTest, ClearRandomizeAndPlainResizeResetTheGeneration)
{
    World      world({.width = 10, .height = 10});
    const auto advanced = [&] {
        world.step();
        return world.generation() == 1U;
    };

    ASSERT_TRUE(advanced());
    world.clear();
    EXPECT_EQ(world.generation(), 0U);

    ASSERT_TRUE(advanced());
    world.randomize(0.5, 1);
    EXPECT_EQ(world.generation(), 0U);

    world.clear();
    ASSERT_TRUE(advanced());
    world.resize({.width = 4, .height = 4}, false);
    EXPECT_EQ(world.generation(), 0U);
}

TEST(WorldTest, SetCellsIgnoresOutsidePositionsAndCountsChanges)
{
    World                      world({.width = 5, .height = 5});
    const std::vector<CellPos> cells = {{.x = 0, .y = 0}, {.x = 0, .y = 0}, {.x = -1, .y = 0},
                                        {.x = 5, .y = 5}, {.x = 4, .y = 4}, {.x = 2, .y = -3},
                                        {.x = 4, .y = 0}};
    EXPECT_EQ(world.setCells(cells, kAlive), 3);  // (0, 0) counts once
    EXPECT_EQ(world.population(), 3);
    EXPECT_EQ(world.setCells(cells, kAlive), 0);
    EXPECT_EQ(toAscii(world.cells()),
              "O...O\n"
              ".....\n"
              ".....\n"
              ".....\n"
              "....O\n");
    EXPECT_EQ(world.setCells(std::vector<CellPos>{{4, 4}, {3, 3}}, kDead), 1);
    EXPECT_EQ(world.population(), 2);

    EXPECT_TRUE(world.setCell({3, 3}, kAlive));
    EXPECT_FALSE(world.setCell({3, 3}, kAlive));
    EXPECT_FALSE(world.setCell({5, 3}, kAlive));
    EXPECT_EQ(world.at({3, 3}), kAlive);
    EXPECT_EQ(world.population(), 3);
}

TEST(WorldTest, PopulationStaysExactThroughRandomOperations)
{
    World      world({.width = 20, .height = 20});
    SplitMix64 random(314);
    const auto below = [&](Coord n) {
        return static_cast<Coord>(random() % static_cast<std::uint64_t>(n));
    };
    for (int i = 0; i < 400; ++i)
    {
        const std::uint64_t operation = random() % 10;
        if (operation < 4)
        {
            std::vector<CellPos> stroke;
            stroke.reserve(20);
            for (int k = 0; k < 20; ++k)  // a few land outside the world
            {
                stroke.push_back({.x = below(world.extent().width + 4) - 2,
                                  .y = below(world.extent().height + 4) - 2});
            }
            world.setCells(stroke, random() % 2 == 0 ? kAlive : kDead);
        }
        else if (operation < 7)
        {
            world.step();
        }
        else if (operation == 7)
        {
            world.randomize(static_cast<double>(below(101)) / 100.0, random());
        }
        else if (operation == 8)
        {
            world.resize({.width = 1 + below(40), .height = 1 + below(40)}, random() % 4 != 0);
        }
        else if (random() % 3 == 0)
        {
            world.clear();
        }
        else
        {
            world.setTopology(kTopologies.at(random() % kTopologies.size()));
        }
        ASSERT_EQ(world.population(), world.cells().countAlive()) << "after operation " << i;
    }
}

// World::randomize(0.3, seed), computed serially: row y uses
// SplitMix64(SplitMix64(seed ^ (0x9E3779B97F4A7C15 * (y + 1)))()), and each number supplies eight
// cells, lowest byte first. A cell lives if its byte is below 77.
Grid serialRandomize(Extent extent, std::uint64_t seed)
{
    const unsigned threshold = 77;  // lround(0.3 * 256)
    Grid           grid(extent);
    for (Coord y = 0; y < extent.height; ++y)
    {
        const std::uint64_t rowSeed =
            seed ^ (0x9E3779B97F4A7C15U * (static_cast<std::uint64_t>(y) + 1));
        const std::uint64_t hashedSeed = SplitMix64(rowSeed)();
        SplitMix64          random(hashedSeed);
        std::uint64_t       bytes = 0;
        for (Coord x = 0; x < extent.width; ++x)
        {
            bytes = x % 8 == 0 ? random() : bytes >> 8;
            grid.set({.x = x, .y = y}, (bytes & 0xFF) < threshold ? kAlive : kDead);
        }
    }
    return grid;
}

TEST(WorldTest, RandomizeFollowsThePerRowFormula)
{
    // randomize() splits the rows with suggestedBandCount(), so on a host with at least 16 hardware
    // threads these sizes use 1, 4, 8 and 16 bands. Any other split must give the same cells.
    const std::uint64_t seed = 0xC0FFEE;
    for (const Extent extent :
         {Extent{.width = 100, .height = 50}, Extent{.width = 1001, .height = 500},
          Extent{.width = 1024, .height = 1024}, Extent{.width = 2048, .height = 1024}})
    {
        SCOPED_TRACE(std::format("{}x{}, {} bands", extent.width, extent.height,
                                 suggestedBandCount(extent.cellCount())));
        World world(extent);
        world.randomize(0.3, seed);
        const Grid expected = serialRandomize(extent, seed);
        EXPECT_EQ(world.cells(), expected);
        EXPECT_EQ(world.population(), expected.countAlive());
    }

    World world({.width = 300, .height = 200});
    world.randomize(0.3, seed);
    World again({.width = 300, .height = 200});
    again.randomize(0.3, seed);
    EXPECT_EQ(again.cells(), world.cells());
    again.randomize(0.3, seed + 1);
    EXPECT_NE(again.cells(), world.cells());
}

// Seeding each row with an unhashed seed made small seeds produce rows that repeat an earlier row
// shifted by 8 cells per row of distance (seed 0: every row; seed 1: every second row), with a
// biased density.
TEST(WorldTest, RandomizeRowsAreIndependentForSmallSeeds)
{
    const Extent extent{.width = 1000, .height = 1000};
    const auto   width = static_cast<std::size_t>(extent.width);
    World        world(extent);
    for (const std::uint64_t seed : {0U, 1U})
    {
        SCOPED_TRACE(std::format("seed {}", seed));
        world.randomize(0.25, seed);
        // Five standard deviations of the live fraction of 10^6 cells that each live with
        // probability 1/4.
        EXPECT_NEAR(static_cast<double>(world.population()) / 1e6, 0.25, 0.0022);

        int shiftedCopies = 0;
        for (Coord y = 0; y < extent.height; ++y)
        {
            for (Coord distance = 1; distance <= 16 && y + distance < extent.height; ++distance)
            {
                const std::size_t           shift = 8 * static_cast<std::size_t>(distance);
                const std::span<const Cell> row   = world.cells().row(y);
                const std::span<const Cell> later = world.cells().row(y + distance);
                if (std::ranges::equal(row.subspan(shift), later.first(width - shift)))
                {
                    ++shiftedCopies;
                }
            }
        }
        EXPECT_EQ(shiftedCopies, 0);
    }
}

TEST(WorldTest, RandomizeDensityLimits)
{
    World world({.width = 300, .height = 200});
    world.randomize(0.0, 5);
    EXPECT_EQ(world.population(), 0);
    world.randomize(1.0, 5);
    EXPECT_EQ(world.population(), world.extent().cellCount());
    world.randomize(-3.0, 5);  // clamped
    EXPECT_EQ(world.population(), 0);
    world.randomize(7.0, 5);
    EXPECT_EQ(world.population(), world.extent().cellCount());
}

TEST(WorldTest, RandomizeHitsTheRequestedDensity)
{
    World      world({.width = 512, .height = 512});
    const auto cells = static_cast<double>(world.extent().cellCount());
    for (const double density : {0.1, 0.25, 0.5, 0.9})
    {
        world.randomize(density, 17);
        EXPECT_NEAR(static_cast<double>(world.population()) / cells, density, 0.01)
            << "density " << density;
    }
}

TEST(WorldTest, ResizeKeepsThePatternCentredWhenGrowing)
{
    World world({.width = 3, .height = 2});
    draw(world, {
                    "O.O",
                    ".OO",
                });
    world.resize({.width = 7, .height = 5}, true);  // offset ((7 - 3) / 2, (5 - 2) / 2) = (2, 1)
    EXPECT_EQ(toAscii(world.cells()),
              ".......\n"
              "..O.O..\n"
              "...OO..\n"
              ".......\n"
              ".......\n");
    EXPECT_EQ(world.population(), 4);
}

TEST(WorldTest, ResizeKeepsTheCentreWhenShrinking)
{
    World world({.width = 7, .height = 6});
    draw(world, {
                    "O......",
                    ".OOOOO.",
                    ".O...O.",
                    ".O.O.O.",
                    ".OOOOO.",
                    "......O",
                });
    world.resize({.width = 4, .height = 3},
                 true);  // offset ((4 - 7) / 2, (3 - 6) / 2) = (-1, -1), truncated toward zero
    EXPECT_EQ(toAscii(world.cells()),
              "OOOO\n"
              "O...\n"
              "O.O.\n");
    EXPECT_EQ(world.population(), 7);
}

TEST(WorldTest, GrowingAndShrinkingBackRestoresThePattern)
{
    for (const Extent from : {Extent{.width = 4, .height = 4}, Extent{.width = 5, .height = 5},
                              Extent{.width = 4, .height = 5}})
    {
        for (const Extent to : {Extent{.width = 7, .height = 7}, Extent{.width = 8, .height = 8},
                                Extent{.width = 7, .height = 8}, Extent{.width = 100, .height = 1}})
        {
            World world(from);
            world.randomize(0.5, 3);
            const Grid original = world.cells();
            world.resize({.width  = std::max(from.width, to.width),
                          .height = std::max(from.height, to.height)},
                         true);
            world.resize(from, true);
            EXPECT_EQ(world.cells(), original)
                << from.width << "x" << from.height << " via " << to.width << "x" << to.height;
        }
    }
}

TEST(WorldTest, ResizeWithoutKeepingClears)
{
    World world({.width = 6, .height = 6});
    draw(world, kGlider);
    world.step();
    world.resize({.width = 9, .height = 3}, false);
    EXPECT_EQ(world.extent(), (Extent{9, 3}));
    EXPECT_EQ(world.population(), 0);
    EXPECT_EQ(world.cells().countAlive(), 0);
    EXPECT_EQ(world.generation(), 0U);
}

TEST(WorldTest, OneByOneWorld)
{
    World world({.width = 3, .height = 3});
    world.setCell({.x = 1, .y = 1}, kAlive);
    world.resize({.width = 1, .height = 1}, true);  // the centre cell survives
    EXPECT_EQ(world.population(), 1);
    EXPECT_EQ(world.at({0, 0}), kAlive);

    // On a 1×1 torus all eight neighbours are the cell itself: B3/S23 kills it, B/S8 keeps it.
    world.setRule(Rule::parse("B/S8").value());
    world.step();
    EXPECT_EQ(world.population(), 1);
    world.setRule(Rule{});
    world.step();
    EXPECT_EQ(world.population(), 0);

    world.setCell({.x = 0, .y = 0}, kAlive);
    world.resize({.width = 3, .height = 3}, true);
    EXPECT_EQ(toAscii(world.cells()),
              "...\n"
              ".O.\n"
              "...\n");
}

// Allocating an absurd size must throw without touching the world. Sanitizer allocators abort on
// such requests instead of throwing, so the test is skipped there. (GCC 14 and Clang have
// __has_feature; MSVC does not, and an unknown macro called in an #if is an error there.)
#ifdef __has_feature
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define WXLIFE_SANITIZED_ALLOCATOR
#endif
#elif defined(__SANITIZE_ADDRESS__)  // MSVC's AddressSanitizer
#define WXLIFE_SANITIZED_ALLOCATOR
#endif
#ifdef WXLIFE_SANITIZED_ALLOCATOR
constexpr bool kSanitizedAllocator = true;
#else
constexpr bool kSanitizedAllocator = false;
#endif

TEST(WorldTest, FailedResizeLeavesTheWorldUnchanged)
{
    if constexpr (kSanitizedAllocator)
    {
        GTEST_SKIP() << "sanitizer allocators do not throw std::bad_alloc";
    }

    World world({.width = 8, .height = 8});
    draw(world, kGlider, {.x = 2, .y = 2});
    world.step();
    const Grid before = world.cells();

    const Coord huge = std::numeric_limits<Coord>::max() - 2;  // about 4 EiB per grid
    EXPECT_THROW(world.resize({huge, huge}, true), std::bad_alloc);
    EXPECT_EQ(world.extent(), (Extent{8, 8}));
    EXPECT_EQ(world.cells(), before);
    EXPECT_EQ(world.generation(), 1U);
    EXPECT_EQ(world.population(), 5);

    EXPECT_THROW(world.resize({huge, huge}, false), std::bad_alloc);
    EXPECT_EQ(world.cells(), before);
    EXPECT_EQ(world.generation(), 1U);
}

TEST(WorldTest, SetRuleChangesTheDynamics)
{
    // In HighLife (B36/S23) this pattern copies itself every 12 generations; in Conway's Life it
    // does not.
    const Pattern replicator = {
        "..OOO", ".O..O", "O...O", "O..O.", "OOO..",
    };
    World world({.width = 21, .height = 21});
    world.setRule(Rule::parse("B36/S23").value());
    draw(world, replicator, {.x = 8, .y = 8});
    for (int g = 0; g < 12; ++g)
    {
        world.step();
    }

    World copies({.width = 21, .height = 21});
    draw(copies, replicator, {.x = 6, .y = 6});
    draw(copies, replicator, {.x = 10, .y = 10});
    EXPECT_EQ(world.cells(), copies.cells());
    EXPECT_EQ(world.population(), 24);

    World conway({.width = 21, .height = 21});
    draw(conway, replicator, {.x = 8, .y = 8});
    for (int g = 0; g < 12; ++g)
    {
        conway.step();
    }
    EXPECT_NE(conway.cells(), copies.cells());
}

TEST(WorldTest, SetTopologyChangesTheNextStep)
{
    // A full row in a 3-high world: with wrapping, the rows above and below both see three
    // neighbours.
    const Pattern row = {
        "...",
        "OOO",
        "...",
    };
    World world({.width = 3, .height = 3}, Rule{}, Topology::BOUNDED);
    draw(world, row);
    world.step();
    EXPECT_EQ(toAscii(world.cells()),
              ".O.\n"
              ".O.\n"
              ".O.\n");

    world.clear();
    draw(world, row);
    world.setTopology(Topology::TORUS);
    EXPECT_EQ(world.topology(), Topology::TORUS);
    world.step();
    EXPECT_EQ(toAscii(world.cells()),
              "OOO\n"
              "OOO\n"
              "OOO\n");
}

TEST(WorldTest, SetStepperKeepsTheState)
{
    World banded({.width = 30, .height = 20});
    banded.randomize(0.4, 8);
    for (int g = 0; g < 5; ++g)
    {
        banded.step();
    }

    World reference({.width = 30, .height = 20});
    reference.randomize(0.4, 8);
    for (int g = 0; g < 5; ++g)
    {
        reference.step();
    }
    const Grid      before     = reference.cells();
    const CellCount population = reference.population();

    reference.setStepper(std::make_unique<ReferenceStepper>());
    EXPECT_EQ(reference.stepper().kind(), StepperKind::REFERENCE);
    EXPECT_EQ(reference.cells(), before);
    EXPECT_EQ(reference.population(), population);
    EXPECT_EQ(reference.generation(), 5U);

    for (int g = 0; g < 20; ++g)
    {
        banded.step();
        reference.step();
    }
    EXPECT_EQ(reference.cells(), banded.cells());
    EXPECT_EQ(reference.population(), banded.population());
}

// ---- Langton's ant
// ---------------------------------------------------------------------------------

TEST(WorldTest, TheAntModeMovesEveryAntOncePerGeneration)
{
    World world({.width = 10, .height = 10});
    world.setAutomaton(Automaton::LANGTON_ANT);
    world.setAnts(std::vector<Ant>{{.position = {.x = 2, .y = 2}, .heading = Heading::NORTH},
                                   {.position = {.x = 7, .y = 7}, .heading = Heading::SOUTH}});

    world.step();
    EXPECT_EQ(world.generation(), 1U);
    EXPECT_EQ(world.population(), 2);  // each ant lit the cell it stood on
    EXPECT_EQ(world.at({2, 2}), kAlive);
    EXPECT_EQ(world.at({7, 7}), kAlive);
    EXPECT_EQ(world.ants()[0], (Ant{{3, 2}, Heading::EAST}));
    EXPECT_EQ(world.ants()[1], (Ant{{6, 7}, Heading::WEST}));
    EXPECT_EQ(world.population(), world.cells().countAlive());
}

TEST(WorldTest, AntsShareTheGridInIndexOrder)
{
    // Both ants start on the same dead cell, so the first lights it and the second finds it alight.
    World world({.width = 9, .height = 9});
    world.setAutomaton(Automaton::LANGTON_ANT);
    world.setAnts(std::vector<Ant>{{.position = {.x = 4, .y = 4}, .heading = Heading::NORTH},
                                   {.position = {.x = 4, .y = 4}, .heading = Heading::NORTH}});

    world.step();
    EXPECT_EQ(world.at({4, 4}), kDead);
    EXPECT_EQ(world.population(), 0);
    EXPECT_EQ(world.ants()[0], (Ant{{5, 4}, Heading::EAST}));  // turned right on a dead cell
    EXPECT_EQ(world.ants()[1], (Ant{{3, 4}, Heading::WEST}));  // turned left on a live one
}

TEST(WorldTest, TheAntAlwaysWrapsWhateverTheTopology)
{
    for (const Topology topology : kTopologies)
    {
        SCOPED_TRACE(toString(topology));
        World world({.width = 4, .height = 4}, Rule{}, topology);
        world.setAutomaton(Automaton::LANGTON_ANT);
        world.setAnts(
            std::vector<Ant>{{.position = {.x = 0, .y = 0},
                              .heading  = Heading::WEST}});  // turns right to north, off the top

        world.step();
        EXPECT_EQ(world.ants()[0], (Ant{{0, 3}, Heading::NORTH}));
        EXPECT_EQ(world.at({0, 0}), kAlive);
    }
}

TEST(WorldTest, SwitchingAutomatonKeepsTheCellsAndSeedsAnAnt)
{
    World world({.width = 9, .height = 7});
    draw(world, kGlider, {.x = 1, .y = 1});
    const Grid before = world.cells();
    EXPECT_EQ(world.automaton(), Automaton::LIFE);
    EXPECT_TRUE(world.ants().empty());

    world.setAutomaton(Automaton::LANGTON_ANT);
    EXPECT_EQ(world.automaton(), Automaton::LANGTON_ANT);
    EXPECT_EQ(world.cells(), before);
    ASSERT_EQ(world.ants().size(), 1U);
    EXPECT_EQ(world.ants()[0], defaultAnt(0, 1, world.extent()));

    world.step();
    const Ant moved = world.ants()[0];
    world.setAutomaton(Automaton::LIFE);  // the ant is kept, and a Life step leaves it alone
    ASSERT_EQ(world.ants().size(), 1U);
    EXPECT_EQ(world.ants()[0], moved);
    world.step();
    EXPECT_EQ(world.ants()[0], moved);
}

TEST(WorldTest, ClearAndRandomizePutTheAntsBack)
{
    World world({.width = 20, .height = 12});
    world.setAutomaton(Automaton::LANGTON_ANT);
    world.resetAnts(3);
    const std::vector<Ant> start(world.ants().begin(), world.ants().end());
    ASSERT_EQ(start.size(), 3U);

    const auto run = [&] {
        for (int g = 0; g < 25; ++g)
        {
            world.step();
        }
    };

    run();
    EXPECT_NE(world.ants()[0], start[0]);
    world.clear();
    EXPECT_TRUE(std::ranges::equal(world.ants(), start));
    EXPECT_EQ(world.generation(), 0U);

    run();
    world.randomize(0.5, 9);
    EXPECT_TRUE(std::ranges::equal(world.ants(), start));
    EXPECT_EQ(world.generation(), 0U);
}

TEST(WorldTest, ResizeMovesTheAntsWithThePattern)
{
    World world({.width = 4, .height = 4});
    world.setAutomaton(Automaton::LANGTON_ANT);
    world.setAnts(std::vector<Ant>{{.position = {.x = 1, .y = 1}, .heading = Heading::EAST}});

    world.resize({.width = 8, .height = 8}, true);  // offset ((8 - 4) / 2, (8 - 4) / 2) = (2, 2)
    EXPECT_EQ(world.ants()[0], (Ant{{3, 3}, Heading::EAST}));
    world.resize({.width = 4, .height = 4}, true);
    EXPECT_EQ(world.ants()[0], (Ant{{1, 1}, Heading::EAST}));

    // A world that cropped an ant pulls it back to the nearest edge.
    world.setAnts(std::vector<Ant>{{.position = {.x = 3, .y = 3}, .heading = Heading::NORTH}});
    world.resize({.width = 2, .height = 2}, true);  // offset (-1, -1)
    EXPECT_EQ(world.ants()[0].position, (CellPos{1, 1}));
    EXPECT_TRUE(world.extent().contains(world.ants()[0].position));

    world.resize({.width = 6, .height = 5}, false);  // nothing kept, so the ants start over
    EXPECT_EQ(world.ants()[0], defaultAnt(0, 1, (Extent{6, 5})));
}

TEST(WorldTest, ToggleAntAtAddsAndRemoves)
{
    World world({.width = 6, .height = 6});
    EXPECT_TRUE(world.toggleAntAt({2, 3}));
    ASSERT_EQ(world.ants().size(), 1U);
    EXPECT_EQ(world.ants()[0], (Ant{{2, 3}, Heading::NORTH}));

    EXPECT_FALSE(world.toggleAntAt({2, 3}));
    EXPECT_TRUE(world.ants().empty());

    EXPECT_FALSE(world.toggleAntAt({-1, 0}));  // outside the world
    EXPECT_FALSE(world.toggleAntAt({6, 6}));
    EXPECT_TRUE(world.ants().empty());

    World crowded({.width = kMaxAnts + 1, .height = 1});
    for (Coord x = 0; x < kMaxAnts; ++x)
    {
        EXPECT_TRUE(crowded.toggleAntAt({x, 0})) << "ant " << x;
    }
    EXPECT_FALSE(crowded.toggleAntAt({kMaxAnts, 0}));  // kMaxAnts is the limit
    EXPECT_EQ(crowded.ants().size(), static_cast<std::size_t>(kMaxAnts));
}

// Langton's ant is chaotic for about ten thousand moves and then starts building a "highway": the
// same 104 moves over and over, each time two cells further down the diagonal and twelve live cells
// heavier. These numbers pin the turn rule, the wrapping and World's incremental population count
// at once.
TEST(WorldTest, TheAntBuildsTheKnownHighway)
{
    constexpr int   kChaos  = 9977;  // moves before the highway begins
    constexpr int   kPeriod = 104;
    constexpr Coord kShiftX = -2;
    constexpr Coord kShiftY = 2;

    // Wide enough that the ant never reaches an edge here, so wrapping cannot disturb the pattern.
    World world({.width = 128, .height = 128});
    world.setAutomaton(Automaton::LANGTON_ANT);
    const CellPos start = world.ants()[0].position;
    const auto    run   = [&](int moves) {
        for (int i = 0; i < moves; ++i)
        {
            world.step();
        }
    };

    run(kChaos);
    EXPECT_EQ(world.population(), 715);
    EXPECT_EQ(world.ants()[0], (Ant{{start.x - 15, start.y - 10}, Heading::WEST}));

    for (int period = 0; period < 10; ++period)
    {
        SCOPED_TRACE(std::format("period {}", period));
        const Ant       before     = world.ants()[0];
        const CellCount population = world.population();
        run(kPeriod);
        EXPECT_EQ(world.ants()[0].heading, before.heading);
        EXPECT_EQ(world.ants()[0].position,
                  (CellPos{before.position.x + kShiftX, before.position.y + kShiftY}));
        EXPECT_EQ(world.population(), population + 12);
    }

    // By now the ant has left the chaotic core behind, so its whole neighbourhood is a plain copy.
    const Grid    before = world.cells();
    const CellPos at     = world.ants()[0].position;
    run(kPeriod);
    const CellPos   moved   = world.ants()[0].position;
    constexpr Coord kRadius = 16;
    for (Coord dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (Coord dx = -kRadius; dx <= kRadius; ++dx)
        {
            ASSERT_EQ(before.at({at.x + dx, at.y + dy}),
                      world.cells().at({moved.x + dx, moved.y + dy}))
                << "at (" << dx << ", " << dy << ")";
        }
    }
    EXPECT_EQ(world.population(), world.cells().countAlive());
}

// -------------------------------------------------------------------------------------------------
// Unbounded worlds
// -------------------------------------------------------------------------------------------------

constexpr std::uint64_t kPlaneBudget = std::uint64_t{1} << 28;

// The live cells of any world, as (x, y) pairs.
std::vector<std::pair<std::int64_t, std::int64_t>> liveCells(const World& world)
{
    std::vector<std::pair<std::int64_t, std::int64_t>> cells;
    if (world.kind() == WorldKind::UNBOUNDED)
    {
        if (const std::optional<UniverseRect> bounds = world.plane().bounds())
        {
            world.plane().forEachBlock(*bounds, 0,
                                       [&](UniversePos p) { cells.emplace_back(p.x, p.y); });
        }
    }
    else
    {
        for (Coord y = 0; y < world.extent().height; ++y)
        {
            for (Coord x = 0; x < world.extent().width; ++x)
            {
                if (world.at({.x = x, .y = y}) == kAlive)
                {
                    cells.emplace_back(x, y);
                }
            }
        }
    }
    std::ranges::sort(cells);
    return cells;
}

TEST(WorldTest, BecomingUnboundedKeepsThePatternAndTheGeneration)
{
    World world({.width = 9, .height = 7}, Rule{}, Topology::BOUNDED);
    draw(world, kGlider, {.x = 1, .y = 1});
    world.step();
    world.step();
    const auto before = liveCells(world);
    world.makeUnbounded(true, kPlaneBudget);
    EXPECT_EQ(world.kind(), WorldKind::UNBOUNDED);
    EXPECT_EQ(world.extent(), (Extent{}));
    EXPECT_EQ(world.generation(), 2U);
    EXPECT_EQ(world.population(), 5);
    // The grid's centre cell, (4, 3), is the plane's (0, 0).
    std::vector<std::pair<std::int64_t, std::int64_t>> moved;
    moved.reserve(before.size());
    for (const auto& [x, y] : before)
    {
        moved.emplace_back(x - 4, y - 3);
    }
    EXPECT_EQ(liveCells(world), moved);
    EXPECT_EQ(world.cellAt({.x = moved.front().first, .y = moved.front().second}), kAlive);
    EXPECT_EQ(world.cellAt({.x = -1'000'000, .y = 5}), kDead);

    // It steps on as the grid would have, without edges.
    ASSERT_TRUE(world.stepPlane(2).has_value());
    EXPECT_EQ(world.generation(), 6U);
    EXPECT_EQ(world.population(), 5);

    // And back: the plane's (0, 0) becomes the new grid's centre cell, (10, 10).
    const auto onPlane = liveCells(world);
    world.resize({.width = 21, .height = 21}, true);
    EXPECT_EQ(world.kind(), WorldKind::FIXED_SIZE);
    EXPECT_EQ(world.generation(), 6U);
    EXPECT_EQ(world.population(), 5);
    std::vector<std::pair<std::int64_t, std::int64_t>> back;
    back.reserve(onPlane.size());
    for (const auto& [x, y] : onPlane)
    {
        back.emplace_back(x + 10, y + 10);
    }
    EXPECT_EQ(liveCells(world), back);
}

TEST(WorldTest, ANewPlaneStartsEmpty)
{
    World world({.width = 9, .height = 7});
    draw(world, kGlider);
    world.step();
    world.makeUnbounded(false, kPlaneBudget);
    EXPECT_EQ(world.kind(), WorldKind::UNBOUNDED);
    EXPECT_EQ(world.population(), 0);
    EXPECT_EQ(world.generation(), 0U);
    world.resize({.width = 5, .height = 5}, false);
    EXPECT_EQ(world.kind(), WorldKind::FIXED_SIZE);
    EXPECT_EQ(world.population(), 0);
}

TEST(WorldTest, APlaneTakesCellsRulesAndClears)
{
    World world({.width = 4, .height = 4});
    world.makeUnbounded(false, kPlaneBudget);
    constexpr std::int64_t         kFar = std::int64_t{1} << 40;
    const std::vector<UniversePos> cells{{.x = -kFar, .y = 0}, {.x = kFar, .y = kFar}};
    EXPECT_EQ(world.setCells(cells, kAlive), 2);
    EXPECT_EQ(world.population(), 2);
    EXPECT_EQ(world.cellAt({.x = kFar, .y = kFar}), kAlive);
    // Patterns come with 32-bit cells and an offset.
    const std::vector<CellPos> block{
        {.x = 0, .y = 0}, {.x = 1, .y = 0}, {.x = 0, .y = 1}, {.x = 1, .y = 1}};
    EXPECT_EQ(world.setCells(block, kAlive, {.x = -5, .y = -5}), 4);
    EXPECT_EQ(world.cellAt({.x = -4, .y = -4}), kAlive);

    world.setRule(Rule::parse("B36/S23").value());
    EXPECT_EQ(world.plane().rule().toString(), "B36/S23");

    world.clear();
    EXPECT_EQ(world.population(), 0);
    EXPECT_EQ(world.generation(), 0U);
    EXPECT_EQ(world.kind(), WorldKind::UNBOUNDED);
}

TEST(WorldTest, RandomizingAPlaneFillsJustTheArea)
{
    World world({.width = 4, .height = 4});
    world.makeUnbounded(false, kPlaneBudget);
    const UniverseRect area{.x0 = -50, .y0 = 1000, .x1 = 30, .y1 = 1040};
    world.randomize(0.5, 42, area);
    const auto cells = liveCells(world);
    ASSERT_FALSE(cells.empty());
    for (const auto& [x, y] : cells)
    {
        EXPECT_TRUE(x >= area.x0 && x < area.x1 && y >= area.y0 && y < area.y1) << x << ", " << y;
    }
    // About half of the 80 × 40 cells, and the same cells for the same seed.
    EXPECT_GT(world.population(), 1200);
    EXPECT_LT(world.population(), 2000);
    world.randomize(0.5, 42, area);
    EXPECT_EQ(liveCells(world), cells);
    EXPECT_EQ(world.generation(), 0U);
}

TEST(WorldTest, FarCellsCannotWrapIntoAGrid)
{
    // 2^32 + 3 would be 3 if it were cut to 32 bits first.
    World                          world({.width = 8, .height = 8});
    constexpr std::int64_t         kWrapped = (std::int64_t{1} << 32) + 3;
    const std::vector<UniversePos> cells{{.x = kWrapped, .y = 3}, {.x = 3, .y = -kWrapped}};
    EXPECT_EQ(world.setCells(cells, kAlive), 0);
    EXPECT_EQ(world.population(), 0);
    EXPECT_EQ(world.cellAt({.x = kWrapped, .y = 3}), kDead);
    const std::vector<UniversePos> inside{{.x = 3, .y = 3}};
    EXPECT_EQ(world.setCells(inside, kAlive), 1);
    EXPECT_EQ(world.cellAt({.x = 3, .y = 3}), kAlive);
}

}  // namespace
}  // namespace wxLife::core
