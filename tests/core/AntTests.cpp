#include "wxLife/core/Ant.h"

#include <algorithm>
#include <array>

#include <gtest/gtest.h>

#include "support/AsciiGrid.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

using test::gridFromAscii;

// Turning and stepping are arithmetic, so their whole contract is checked at compile time.
static_assert(std::ranges::all_of(kHeadings,
                                  [](Heading h) { return turnLeft(turnRight(h)) == h; }));
static_assert(std::ranges::all_of(kHeadings, [](Heading h) {
    return turnRight(turnRight(turnRight(turnRight(h)))) == h;
}));
static_assert(turnRight(Heading::NORTH) == Heading::EAST);  // the four are in clockwise order
static_assert(turnRight(Heading::EAST) == Heading::SOUTH);
static_assert(turnRight(Heading::SOUTH) == Heading::WEST);
static_assert(turnRight(Heading::WEST) == Heading::NORTH);
static_assert(toString(Heading::NORTH) == "north");

// forward() steps inside a 4 × 3 world and wraps at each of its four edges.
static_assert(forward({.x = 1, .y = 1}, Heading::NORTH, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 0});
static_assert(forward({.x = 1, .y = 0}, Heading::NORTH, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 2});
static_assert(forward({.x = 3, .y = 1}, Heading::EAST, {.width = 4, .height = 3}) ==
              CellPos{.x = 0, .y = 1});
static_assert(forward({.x = 1, .y = 2}, Heading::SOUTH, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 0});
static_assert(forward({.x = 0, .y = 1}, Heading::WEST, {.width = 4, .height = 3}) ==
              CellPos{.x = 3, .y = 1});

// One ant starts in the middle, where Langton's ant draws its classic pattern; several share the
// middle row.
static_assert(defaultAnt(0, 1, {.width = 9, .height = 9}) ==
              Ant{.position = {.x = 4, .y = 4}, .heading = Heading::NORTH});
static_assert(defaultAnt(0, 3, {.width = 12, .height = 4}).position == CellPos{.x = 3, .y = 2});
static_assert(defaultAnt(1, 3, {.width = 12, .height = 4}).position == CellPos{.x = 6, .y = 2});
static_assert(defaultAnt(2, 3, {.width = 12, .height = 4}).position == CellPos{.x = 9, .y = 2});

TEST(AntTest, TurnsRightOnADeadCellAndLeftOnALiveOne)
{
    Grid grid = gridFromAscii({"...", ".O.", "..."});

    Ant onDead{.position = {.x = 0, .y = 1}, .heading = Heading::NORTH};
    EXPECT_EQ(advance(onDead, grid), 1);  // the cell came alive
    EXPECT_EQ(onDead.heading, Heading::EAST);
    EXPECT_EQ(grid.at({0, 1}), kAlive);
    EXPECT_EQ(onDead.position, (CellPos{1, 1}));

    Ant onAlive{.position = {.x = 1, .y = 1}, .heading = Heading::NORTH};
    EXPECT_EQ(advance(onAlive, grid), -1);  // and this one died
    EXPECT_EQ(onAlive.heading, Heading::WEST);
    EXPECT_EQ(grid.at({1, 1}), kDead);
    EXPECT_EQ(onAlive.position, (CellPos{0, 1}));
}

TEST(AntTest, FourMovesDrawABlockAndComeBack)
{
    Grid grid({.width = 5, .height = 5});
    Ant  ant{.position = {.x = 2, .y = 2}, .heading = Heading::NORTH};

    // Turning right on every dead cell walks the ant clockwise around a 2 × 2 block.
    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..O..", ".....", "....."}));
    EXPECT_EQ(ant, (Ant{{3, 2}, Heading::EAST}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", ".....", "....."}));
    EXPECT_EQ(ant, (Ant{{3, 3}, Heading::SOUTH}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", "...O.", "....."}));
    EXPECT_EQ(ant, (Ant{{2, 3}, Heading::WEST}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", "..OO.", "....."}));
    EXPECT_EQ(ant, (Ant{{2, 2}, Heading::NORTH}));  // back where it started, having drawn the block
}

TEST(AntTest, MovingOffAnEdgeWrapsToTheOppositeOne)
{
    // Every cell is dead, so each ant turns right before it moves: it leaves facing `leaving`.
    struct Case
    {
        Ant     start;
        Heading leaving = Heading::NORTH;
        CellPos expected;
    };
    constexpr std::array<Case, 4> kCases{{
        {.start    = {.position = {.x = 1, .y = 0}, .heading = Heading::WEST},
         .leaving  = Heading::NORTH,
         .expected = {.x = 1, .y = 2}},
        {.start    = {.position = {.x = 2, .y = 1}, .heading = Heading::NORTH},
         .leaving  = Heading::EAST,
         .expected = {.x = 0, .y = 1}},
        {.start    = {.position = {.x = 1, .y = 2}, .heading = Heading::EAST},
         .leaving  = Heading::SOUTH,
         .expected = {.x = 1, .y = 0}},
        {.start    = {.position = {.x = 0, .y = 1}, .heading = Heading::SOUTH},
         .leaving  = Heading::WEST,
         .expected = {.x = 2, .y = 1}},
    }};

    for (const Case& scenario : kCases)
    {
        SCOPED_TRACE(toString(scenario.leaving));
        Grid grid({.width = 3, .height = 3});
        Ant  ant = scenario.start;
        advance(ant, grid);
        EXPECT_EQ(ant.heading, scenario.leaving);
        EXPECT_EQ(ant.position, scenario.expected);
    }
}

}  // namespace
}  // namespace wxLife::core
