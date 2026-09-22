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
static_assert(turnRight(Heading::North) == Heading::East);  // the four are in clockwise order
static_assert(turnRight(Heading::East) == Heading::South);
static_assert(turnRight(Heading::South) == Heading::West);
static_assert(turnRight(Heading::West) == Heading::North);
static_assert(toString(Heading::North) == "north");

// forward() steps inside a 4 × 3 world and wraps at each of its four edges.
static_assert(forward({.x = 1, .y = 1}, Heading::North, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 0});
static_assert(forward({.x = 1, .y = 0}, Heading::North, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 2});
static_assert(forward({.x = 3, .y = 1}, Heading::East, {.width = 4, .height = 3}) ==
              CellPos{.x = 0, .y = 1});
static_assert(forward({.x = 1, .y = 2}, Heading::South, {.width = 4, .height = 3}) ==
              CellPos{.x = 1, .y = 0});
static_assert(forward({.x = 0, .y = 1}, Heading::West, {.width = 4, .height = 3}) ==
              CellPos{.x = 3, .y = 1});

// One ant starts in the middle, where Langton's ant draws its classic pattern; several share the
// middle row.
static_assert(defaultAnt(0, 1, {.width = 9, .height = 9}) ==
              Ant{.position = {.x = 4, .y = 4}, .heading = Heading::North});
static_assert(defaultAnt(0, 3, {.width = 12, .height = 4}).position == CellPos{.x = 3, .y = 2});
static_assert(defaultAnt(1, 3, {.width = 12, .height = 4}).position == CellPos{.x = 6, .y = 2});
static_assert(defaultAnt(2, 3, {.width = 12, .height = 4}).position == CellPos{.x = 9, .y = 2});

TEST(AntTest, TurnsRightOnADeadCellAndLeftOnALiveOne)
{
    Grid grid = gridFromAscii({"...", ".O.", "..."});

    Ant onDead{.position = {.x = 0, .y = 1}, .heading = Heading::North};
    EXPECT_EQ(advance(onDead, grid), 1);  // the cell came alive
    EXPECT_EQ(onDead.heading, Heading::East);
    EXPECT_EQ(grid.at({0, 1}), kAlive);
    EXPECT_EQ(onDead.position, (CellPos{1, 1}));

    Ant onAlive{.position = {.x = 1, .y = 1}, .heading = Heading::North};
    EXPECT_EQ(advance(onAlive, grid), -1);  // and this one died
    EXPECT_EQ(onAlive.heading, Heading::West);
    EXPECT_EQ(grid.at({1, 1}), kDead);
    EXPECT_EQ(onAlive.position, (CellPos{0, 1}));
}

TEST(AntTest, FourMovesDrawABlockAndComeBack)
{
    Grid grid({.width = 5, .height = 5});
    Ant  ant{.position = {.x = 2, .y = 2}, .heading = Heading::North};

    // Turning right on every dead cell walks the ant clockwise around a 2 × 2 block.
    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..O..", ".....", "....."}));
    EXPECT_EQ(ant, (Ant{{3, 2}, Heading::East}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", ".....", "....."}));
    EXPECT_EQ(ant, (Ant{{3, 3}, Heading::South}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", "...O.", "....."}));
    EXPECT_EQ(ant, (Ant{{2, 3}, Heading::West}));

    advance(ant, grid);
    EXPECT_EQ(grid, gridFromAscii({".....", ".....", "..OO.", "..OO.", "....."}));
    EXPECT_EQ(ant, (Ant{{2, 2}, Heading::North}));  // back where it started, having drawn the block
}

TEST(AntTest, MovingOffAnEdgeWrapsToTheOppositeOne)
{
    // Every cell is dead, so each ant turns right before it moves: it leaves facing `leaving`.
    struct Case
    {
        Ant     start;
        Heading leaving = Heading::North;
        CellPos expected;
    };
    constexpr std::array<Case, 4> kCases{{
        {.start    = {.position = {.x = 1, .y = 0}, .heading = Heading::West},
         .leaving  = Heading::North,
         .expected = {.x = 1, .y = 2}},
        {.start    = {.position = {.x = 2, .y = 1}, .heading = Heading::North},
         .leaving  = Heading::East,
         .expected = {.x = 0, .y = 1}},
        {.start    = {.position = {.x = 1, .y = 2}, .heading = Heading::East},
         .leaving  = Heading::South,
         .expected = {.x = 1, .y = 0}},
        {.start    = {.position = {.x = 0, .y = 1}, .heading = Heading::South},
         .leaving  = Heading::West,
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
