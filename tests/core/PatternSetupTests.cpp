#include "wxLife/core/PatternSetup.h"

#include <array>
#include <cstdint>
#include <expected>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Demo.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{
namespace
{

constexpr std::uint64_t kLargeBudget = std::uint64_t{1} << 40;

// One live cell in the corner of an area of this size.
Pattern blank(Extent extent)
{
    Pattern pattern;
    pattern.extent = extent;
    pattern.cells  = {{.x = 0, .y = 0}};
    return pattern;
}

TEST(PatternSetupTest, ADemoBringsItsOwnWorld)
{
    constexpr std::array kAnts{Ant{.position = {.x = 3, .y = 4}, .heading = Heading::WEST}};
    const Demo           demo{
        .name      = "Test",
        .about     = "A test.",
        .world     = {.width = 60, .height = 40},
        .topology  = Topology::TORUS,
        .speed     = {.gensPerSecond = 7},
        .view      = CellRect{.x0 = -5, .y0 = -5, .x1 = 15, .y1 = 10},
        .automaton = Automaton::LANGTON_ANT,
        .ants      = kAnts,
    };
    const PatternSetup setup = demoSetup(demo, blank({.width = 10, .height = 5}));
    EXPECT_EQ(setup.world, (Extent{60, 40}));
    EXPECT_EQ(setup.origin, (CellPos{25, 17}));  // centred: (60 - 10) / 2, (40 - 5) / 2
    EXPECT_EQ(setup.topology, Topology::TORUS);
    EXPECT_EQ(setup.rule, Rule{});  // the file names none
    EXPECT_EQ(setup.automaton, Automaton::LANGTON_ANT);
    EXPECT_EQ(setup.ants, (std::vector<Ant>(kAnts.begin(), kAnts.end())));
    EXPECT_EQ(setup.speed, (Speed{.gensPerSecond = 7}));
    ASSERT_TRUE(setup.view.has_value());
    EXPECT_EQ(setup.view.value(), (CellRect{.x0 = 20, .y0 = 12, .x1 = 40, .y1 = 27}));

    // An origin places the pattern there, and a rule in the file wins over Conway's Life.
    Demo placed              = demo;
    placed.origin            = CellPos{.x = 2, .y = 1};
    Pattern highLife         = blank({.width = 10, .height = 5});
    highLife.rule            = Rule::parse("B36/S23").value();
    const PatternSetup moved = demoSetup(placed, highLife);
    EXPECT_EQ(moved.origin, (CellPos{2, 1}));
    EXPECT_EQ(moved.view.value().x0, -3);
    EXPECT_EQ(moved.rule.toString(), "B36/S23");
}

TEST(PatternSetupTest, AFileGetsRoomAroundIt)
{
    const Rule current = Rule::parse("B36/S23").value();

    // At least kMinFileMargin on each side.
    const auto small =
        fileSetup(blank({.width = 3, .height = 3}), current, Topology::TORUS, kLargeBudget);
    ASSERT_TRUE(small.has_value());
    EXPECT_EQ(small->world, (Extent{103, 103}));
    EXPECT_EQ(small->origin, (CellPos{50, 50}));
    // What the file does not say stays as it is.
    EXPECT_EQ(small->rule, current);
    EXPECT_EQ(small->topology, Topology::TORUS);
    EXPECT_FALSE(small->speed.has_value());
    EXPECT_FALSE(small->view.has_value());
    EXPECT_EQ(small->automaton, Automaton::LIFE);

    // Half the pattern's size on each side, once that is more.
    Pattern large  = blank({.width = 400, .height = 200});
    large.rule     = Rule{};
    const auto big = fileSetup(large, current, Topology::BOUNDED, kLargeBudget);
    ASSERT_TRUE(big.has_value());
    EXPECT_EQ(big->world, (Extent{800, 400}));
    EXPECT_EQ(big->origin, (CellPos{200, 100}));
    EXPECT_EQ(big->rule, Rule{});
    EXPECT_EQ(big->topology, Topology::BOUNDED);

    // No more than the side limit, and an empty pattern gets an empty world of margins.
    const auto wide =
        fileSetup(blank({.width = 99'000, .height = 10}), current, Topology::TORUS, kLargeBudget);
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(wide->world, (Extent{kMaxWorldSide, 110}));
    EXPECT_EQ(wide->origin.x, 500);
    const auto empty = fileSetup(Pattern{}, current, Topology::TORUS, kLargeBudget);
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->world, (Extent{100, 100}));
}

TEST(PatternSetupTest, TheMarginsShrinkToFitTheBudget)
{
    // 1000 × 1000 wants 500 on each side. The margins halve until the world fits 1100 × 1100:
    // 500, 250, 125 and 62 are too many, 31 is not.
    const Pattern pattern = blank({.width = 1000, .height = 1000});
    const auto    setup =
        fileSetup(pattern, Rule{}, Topology::TORUS, worldBytes({.width = 1100, .height = 1100}));
    ASSERT_TRUE(setup.has_value());
    EXPECT_EQ(setup->world, (Extent{1062, 1062}));
    EXPECT_EQ(setup->origin, (CellPos{31, 31}));

    // Just the pattern, without any room around it.
    const auto tight =
        fileSetup(pattern, Rule{}, Topology::TORUS, worldBytes({.width = 1000, .height = 1000}));
    ASSERT_TRUE(tight.has_value());
    EXPECT_EQ(tight->world, (Extent{1000, 1000}));

    // Not even that.
    const auto none =
        fileSetup(pattern, Rule{}, Topology::TORUS, worldBytes({.width = 999, .height = 999}));
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error(), ExtentError::OVER_MEMORY_BUDGET);
}

}  // namespace
}  // namespace wxLife::core
