#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "support/AsciiGrid.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

using test::gridFromAscii;
using test::toAscii;

// A padded cell; x and y may be -1 or one past the last interior cell.
Cell padded(const Grid& grid, Coord x, Coord y)
{
    return grid.paddedRow(y)[static_cast<std::size_t>(x) + 1];
}

// True if every ghost cell holds the interior cell on the opposite edge (torus wrap).
bool borderWrapsAround(const Grid& grid)
{
    const auto [width, height] = grid.extent();
    const auto wrap            = [](Coord v, Coord size) { return (v + size) % size; };
    for (Coord y = -1; y <= height; ++y)
    {
        for (Coord x = -1; x <= width; ++x)
        {
            const bool ghost = !grid.extent().contains({.x = x, .y = y});
            if (ghost && padded(grid, x, y) != grid.at({.x = wrap(x, width), .y = wrap(y, height)}))
            {
                return false;
            }
        }
    }
    return true;
}

// True if every ghost cell is dead.
bool borderIsDead(const Grid& grid)
{
    const auto [width, height] = grid.extent();
    for (Coord y = -1; y <= height; ++y)
    {
        for (Coord x = -1; x <= width; ++x)
        {
            if (!grid.extent().contains({.x = x, .y = y}) && padded(grid, x, y) != kDead)
            {
                return false;
            }
        }
    }
    return true;
}

TEST(GridTest, StartsDead)
{
    const Grid grid({.width = 5, .height = 4});
    EXPECT_EQ(grid.extent(), (Extent{5, 4}));
    EXPECT_EQ(grid.countAlive(), 0);
    EXPECT_TRUE(borderIsDead(grid));
}

TEST(GridTest, DefaultGridIsEmpty)
{
    Grid grid;
    EXPECT_EQ(grid.extent(), (Extent{0, 0}));
    EXPECT_EQ(grid.countAlive(), 0);
    EXPECT_EQ(grid.paddedRow(-1).size(), 2U);
    EXPECT_EQ(grid.paddedRow(0).size(), 2U);
    grid.updateBorder(Topology::Torus);  // nothing to wrap: the ghost cells stay dead
    EXPECT_TRUE(borderIsDead(grid));
    EXPECT_EQ(grid, Grid({0, 0}));
}

TEST(GridTest, SetAndAt)
{
    Grid grid({.width = 3, .height = 2});
    grid.set({.x = 2, .y = 1}, kAlive);
    grid.set({.x = 0, .y = 0}, kAlive);
    grid.set({.x = 0, .y = 0}, kDead);
    EXPECT_EQ(grid.at({2, 1}), kAlive);
    EXPECT_EQ(grid.at({0, 0}), kDead);
    EXPECT_EQ(toAscii(grid),
              "...\n"
              "..O\n");
}

TEST(GridTest, RowsViewTheSameCells)
{
    Grid grid = gridFromAscii({
        "O...",
        ".O..",
        "...O",
    });
    EXPECT_EQ(grid.row(0).size(), 4U);
    EXPECT_EQ(grid.paddedRow(-1).size(), 6U);
    EXPECT_EQ(grid.paddedRow(3).size(), 6U);
    for (Coord y = 0; y < 3; ++y)
    {
        for (Coord x = 0; x < 4; ++x)
        {
            const auto i = static_cast<std::size_t>(x);
            EXPECT_EQ(std::as_const(grid).row(y)[i], grid.at({x, y}));
            EXPECT_EQ(grid.paddedRow(y)[i + 1], grid.at({x, y}));
        }
    }
    grid.row(1)[3] = kAlive;  // the mutable row writes through
    EXPECT_EQ(grid.at({3, 1}), kAlive);
}

TEST(GridTest, BoundedBorderIsDeadEvenAfterATorusRefresh)
{
    Grid grid({.width = 4, .height = 3});
    for (Coord y = 0; y < 3; ++y)
    {
        std::ranges::fill(grid.row(y), kAlive);
    }
    grid.updateBorder(Topology::Torus);
    EXPECT_FALSE(borderIsDead(grid));
    grid.updateBorder(Topology::Bounded);
    EXPECT_TRUE(borderIsDead(grid));
    EXPECT_EQ(grid.countAlive(), 12);
}

TEST(GridTest, TorusBorderCopiesTheOppositeCorner)
{
    Grid grid({.width = 5, .height = 4});
    grid.set({.x = 4, .y = 3}, kAlive);  // bottom-right
    grid.updateBorder(Topology::Torus);
    EXPECT_EQ(padded(grid, -1, -1), kAlive);  // top-left ghost
    EXPECT_EQ(padded(grid, 4, -1), kAlive);   // above the cell
    EXPECT_EQ(padded(grid, -1, 3), kAlive);   // left of row 3
    EXPECT_EQ(padded(grid, 5, 4), kDead);

    grid.clear();
    grid.set({.x = 0, .y = 0}, kAlive);  // top-left
    grid.updateBorder(Topology::Torus);
    EXPECT_EQ(padded(grid, 5, 4), kAlive);  // bottom-right ghost
    EXPECT_EQ(padded(grid, -1, -1), kDead);

    grid.clear();
    grid.set({.x = 0, .y = 3}, kAlive);  // bottom-left
    grid.updateBorder(Topology::Torus);
    EXPECT_EQ(padded(grid, 5, -1), kAlive);  // top-right ghost

    grid.clear();
    grid.set({.x = 4, .y = 0}, kAlive);  // top-right
    grid.updateBorder(Topology::Torus);
    EXPECT_EQ(padded(grid, -1, 4), kAlive);  // bottom-left ghost
}

// Lighting one cell at a time shows exactly which ghost cells copy it.
class GridTorusTest : public testing::TestWithParam<Extent>
{ };

TEST_P(GridTorusTest, EveryGhostCellCopiesTheWrappedCell)
{
    const Extent extent = GetParam();
    for (Coord y = 0; y < extent.height; ++y)
    {
        for (Coord x = 0; x < extent.width; ++x)
        {
            Grid grid(extent);
            grid.set({.x = x, .y = y}, kAlive);
            grid.updateBorder(Topology::Torus);
            EXPECT_TRUE(borderWrapsAround(grid)) << "live cell (" << x << ", " << y << ")";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Sizes, GridTorusTest,
                         testing::Values(Extent{5, 4}, Extent{1, 1}, Extent{1, 6}, Extent{6, 1},
                                         Extent{2, 2}),
                         [](const testing::TestParamInfo<Extent>& test) {
                             return std::to_string(test.param.width) + "x" +
                                    std::to_string(test.param.height);
                         });

TEST(GridTest, CountAliveIgnoresTheBorder)
{
    Grid grid = gridFromAscii({
        "O..O",
        "....",
        "O..O",
    });
    grid.updateBorder(Topology::Torus);  // the corners are now copied many times
    EXPECT_EQ(grid.countAlive(), 4);
}

TEST(GridTest, EqualityComparesExtentAndInterior)
{
    Grid       a = gridFromAscii({".O", "O."});
    const Grid b = gridFromAscii({".O", "O."});
    a.updateBorder(Topology::Torus);  // only the ghost cells differ now
    EXPECT_EQ(a, b);
    EXPECT_NE(a, gridFromAscii({".O", "OO"}));
    EXPECT_NE(gridFromAscii({"..", ".."}), gridFromAscii({"...."}));
    EXPECT_NE(gridFromAscii({"..", ".."}), gridFromAscii({".."}));
}

TEST(GridTest, ClearKillsBorderToo)
{
    Grid grid = gridFromAscii({
        "OO",
        "OO",
    });
    grid.updateBorder(Topology::Torus);
    grid.clear();
    EXPECT_EQ(grid.countAlive(), 0);
    EXPECT_TRUE(borderIsDead(grid));
}

TEST(GridTest, AsciiRoundTrip)
{
    const Grid grid = gridFromAscii({
        ".O.",
        "..O",
        "OOO",
    });
    EXPECT_EQ(toAscii(grid),
              ".O.\n"
              "..O\n"
              "OOO\n");
    EXPECT_EQ(grid.countAlive(), 5);
    EXPECT_THROW(static_cast<void>(gridFromAscii({"..", "..."})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gridFromAscii({"x"})), std::invalid_argument);
}

}  // namespace
}  // namespace wxLife::core
