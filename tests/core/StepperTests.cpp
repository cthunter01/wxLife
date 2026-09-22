#include "wxLife/core/Stepper.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "support/AsciiGrid.h"
#include "wxLife/core/BandedStepper.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/Random.h"
#include "wxLife/core/ReferenceStepper.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

using test::gridFromAscii;

using Pattern = std::initializer_list<std::string_view>;

// Replaces `grid` by its successor, after preparing the ghost border; returns the stepper's
// population.
CellCount stepInPlace(Stepper& stepper, Grid& grid, const Rule& rule, Topology topology)
{
    grid.updateBorder(topology);
    Grid            next(grid.extent());
    const CellCount population = stepper.step(grid, next, rule, topology);
    grid                       = std::move(next);
    return population;
}

// An `extent` grid, all dead except `pattern` with its top-left cell at `at`.
Grid place(Pattern pattern, Extent extent, CellPos at)
{
    const Grid source = gridFromAscii(pattern);
    Grid       grid(extent);
    for (Coord y = 0; y < source.extent().height; ++y)
    {
        for (Coord x = 0; x < source.extent().width; ++x)
        {
            grid.set({.x = at.x + x, .y = at.y + y}, source.at({.x = x, .y = y}));
        }
    }
    return grid;
}

// About 3 in 8 cells alive.
Grid randomGrid(Extent extent, std::uint64_t seed)
{
    SplitMix64 random(seed);
    Grid       grid(extent);
    for (Coord y = 0; y < extent.height; ++y)
    {
        for (Coord x = 0; x < extent.width; ++x)
        {
            grid.set({.x = x, .y = y}, random() % 8 < 3 ? kAlive : kDead);
        }
    }
    return grid;
}

// ---- Banded equals Reference
// ------------------------------------------------------------------------

// Every preset, B0/S8 (every dead cell with no live neighbour is born) and a few random rules.
std::vector<Rule> allRules()
{
    std::vector<Rule> rules;
    rules.reserve(kRulePresets.size() + 4);
    for (const NamedRule& preset : kRulePresets)
    {
        rules.push_back(preset.rule);
    }
    rules.push_back(Rule::parse("B0/S8").value());
    SplitMix64 random(2024);
    for (int i = 0; i < 3; ++i)
    {
        rules.emplace_back(static_cast<Rule::Mask>(random()), static_cast<Rule::Mask>(random()));
    }
    return rules;
}

// A smaller set for the larger worlds, where the reference stepper is slow in Debug builds.
std::vector<Rule> someRules()
{
    return {Rule{}, Rule::parse("B36/S23").value(), Rule::parse("B2/S").value(),
            Rule::parse("B0/S8").value(), Rule{0b1'0110'1001, 0b0'1001'0110}};
}

struct EquivalenceCase
{
    Extent            extent;
    std::vector<Rule> rules;
    int               generations = 50;
};

class StepperEquivalenceTest : public testing::TestWithParam<EquivalenceCase>
{ };

TEST_P(StepperEquivalenceTest, BandedMatchesReference)
{
    const EquivalenceCase& param = GetParam();
    constexpr std::array   kMaxThreads{1U, 2U, 7U, 64U};

    ReferenceStepper                      reference;
    std::vector<std::unique_ptr<Stepper>> banded;
    banded.reserve(kMaxThreads.size());
    for (const unsigned threads : kMaxThreads)
    {
        // 1 cell per band: as many bands as rows.
        banded.push_back(std::make_unique<BandedStepper>(threads, 1));
    }

    std::uint64_t seed = 1;
    for (const Topology topology : kTopologies)
    {
        for (const Rule& rule : param.rules)
        {
            SCOPED_TRACE(std::format("{} {}", rule.toString(), toString(topology)));
            Grid grid = randomGrid(param.extent, seed++);
            for (int generation = 0; generation < param.generations; ++generation)
            {
                grid.updateBorder(topology);
                Grid            expected(param.extent);
                const CellCount expectedPopulation = reference.step(grid, expected, rule, topology);
                ASSERT_EQ(expectedPopulation, expected.countAlive());

                for (std::size_t i = 0; i < banded.size(); ++i)
                {
                    Grid            actual(param.extent);
                    const CellCount population = banded[i]->step(grid, actual, rule, topology);
                    ASSERT_EQ(actual, expected)
                        << "generation " << generation << ", maxThreads " << kMaxThreads.at(i);
                    ASSERT_EQ(population, expectedPopulation)
                        << "generation " << generation << ", maxThreads " << kMaxThreads.at(i);
                }
                grid = std::move(expected);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Sizes, StepperEquivalenceTest,
    testing::Values(EquivalenceCase{{1, 1}, allRules()}, EquivalenceCase{{1, 7}, allRules()},
                    EquivalenceCase{{7, 1}, allRules()}, EquivalenceCase{{3, 5}, allRules()},
                    EquivalenceCase{{64, 64}, allRules()}, EquivalenceCase{{257, 131}, someRules()},
                    EquivalenceCase{{1013, 777}, {Rule{}}, 10}),
    [](const testing::TestParamInfo<EquivalenceCase>& test) {
        return std::format("{}x{}", test.param.extent.width, test.param.extent.height);
    });

// ---- Known patterns, for both engines
// -----------------------------------------------------------------

class StepperPatternTest : public testing::TestWithParam<StepperKind>
{
protected:
    // `grid` after `generations` steps of Conway's Life.
    Grid evolve(Grid grid, int generations, Topology topology = Topology::Bounded)
    {
        for (int g = 0; g < generations; ++g)
        {
            stepInPlace(*m_stepper, grid, Rule{}, topology);
        }
        return grid;
    }

    // True if `grid` returns to itself after `period` generations, and not before.
    bool hasPeriod(const Grid& grid, int period)
    {
        for (int g = 1; g < period; ++g)
        {
            if (evolve(grid, g) == grid)
            {
                return false;
            }
        }
        return evolve(grid, period) == grid;
    }

private:
    std::unique_ptr<Stepper> m_stepper = makeStepper(GetParam());
};

TEST_P(StepperPatternTest, StillLifesStayStill)
{
    const Grid block   = gridFromAscii({
        "....",
        ".OO.",
        ".OO.",
        "....",
    });
    const Grid beehive = gridFromAscii({
        "......",
        "..OO..",
        ".O..O.",
        "..OO..",
        "......",
    });
    EXPECT_EQ(evolve(block, 1), block);
    EXPECT_EQ(evolve(block, 5), block);
    EXPECT_EQ(evolve(beehive, 1), beehive);
    EXPECT_EQ(evolve(beehive, 5), beehive);
}

TEST_P(StepperPatternTest, OscillatorsHaveTheirPeriods)
{
    const Grid blinker = gridFromAscii({
        ".....",
        ".....",
        ".OOO.",
        ".....",
        ".....",
    });
    const Grid toad    = gridFromAscii({
        "......",
        "......",
        "..OOO.",
        ".OOO..",
        "......",
        "......",
    });
    const Grid pulsar  = place(
        {
            "..OOO...OOO..",
            ".............",
            "O....O.O....O",
            "O....O.O....O",
            "O....O.O....O",
            "..OOO...OOO..",
            ".............",
            "..OOO...OOO..",
            "O....O.O....O",
            "O....O.O....O",
            "O....O.O....O",
            ".............",
            "..OOO...OOO..",
        },
        {.width = 17, .height = 17}, {.x = 2, .y = 2});
    EXPECT_TRUE(hasPeriod(blinker, 2));
    EXPECT_EQ(evolve(blinker, 1), gridFromAscii({
                                      ".....",
                                      "..O..",
                                      "..O..",
                                      "..O..",
                                      ".....",
                                  }));
    EXPECT_TRUE(hasPeriod(toad, 2));
    EXPECT_TRUE(hasPeriod(pulsar, 3));
}

TEST_P(StepperPatternTest, GliderMovesOneCellDiagonallyEveryFourGenerations)
{
    const Pattern glider = {
        ".O.",
        "..O",
        "OOO",
    };
    const Grid start = place(glider, {.width = 12, .height = 10}, {.x = 1, .y = 1});
    EXPECT_EQ(evolve(start, 4), place(glider, {12, 10}, {2, 2}));
    EXPECT_EQ(evolve(start, 16), place(glider, {12, 10}, {5, 5}));
    // On a torus it crosses the edges and comes back.
    const Grid torusStart = place(glider, {.width = 6, .height = 6}, {.x = 1, .y = 1});
    EXPECT_EQ(evolve(torusStart, 24, Topology::Torus), torusStart);
}

TEST_P(StepperPatternTest, GliderHittingABoundedCornerBecomesABlock)
{
    const Grid start = place({".O.", "..O", "OOO"}, {.width = 6, .height = 6}, {.x = 1, .y = 1});
    const Grid block = gridFromAscii({
        "......",
        "......",
        "......",
        "......",
        "....OO",
        "....OO",
    });
    EXPECT_NE(evolve(start, 10), block);
    EXPECT_EQ(evolve(start, 11), block);
    EXPECT_EQ(evolve(start, 30), block);
}

TEST_P(StepperPatternTest, ReturnsThePopulationOfTheResult)
{
    const std::unique_ptr<Stepper> stepper = makeStepper(GetParam());
    for (const Topology topology : kTopologies)
    {
        for (const Rule& rule : someRules())
        {
            Grid grid = randomGrid({.width = 37, .height = 23}, 99);
            for (int g = 0; g < 10; ++g)
            {
                const CellCount population = stepInPlace(*stepper, grid, rule, topology);
                ASSERT_EQ(population, grid.countAlive()) << rule.toString() << ", generation " << g;
            }
        }
    }
}

TEST_P(StepperPatternTest, ReportsItsKind)
{
    EXPECT_EQ(makeStepper(GetParam())->kind(), GetParam());
    EXPECT_EQ(makeStepper(GetParam(), 3)->kind(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Engines, StepperPatternTest, testing::ValuesIn(kStepperKinds),
                         [](const testing::TestParamInfo<StepperKind>& test) {
                             return std::string(toString(test.param));
                         });

TEST(StepperTest, KindNames)
{
    EXPECT_EQ(toString(StepperKind::Banded), "Banded");
    EXPECT_EQ(toString(StepperKind::Reference), "Reference");
}

TEST(StepperTest, ParseKindIgnoresCase)
{
    for (const StepperKind kind : kStepperKinds)
    {
        EXPECT_EQ(parseStepperKind(toString(kind)), kind) << toString(kind);
    }
    EXPECT_EQ(parseStepperKind("banded"), StepperKind::Banded);
    EXPECT_EQ(parseStepperKind("REFERENCE"), StepperKind::Reference);
    EXPECT_FALSE(parseStepperKind(""));
    EXPECT_FALSE(parseStepperKind("band"));
    EXPECT_FALSE(parseStepperKind("banded "));
}

TEST(StepperTest, EmptyGridsStepToEmptyGrids)
{
    for (const StepperKind kind : kStepperKinds)
    {
        const std::unique_ptr<Stepper> stepper = makeStepper(kind);
        for (const Extent extent :
             {Extent{.width = 0, .height = 0}, Extent{.width = 0, .height = 3},
              Extent{.width = 3, .height = 0}})
        {
            Grid grid(extent);
            EXPECT_EQ(stepInPlace(*stepper, grid, Rule::parse("B0/S").value(), Topology::Torus), 0);
            EXPECT_EQ(grid.extent(), extent);
        }
    }
}

}  // namespace
}  // namespace wxLife::core
