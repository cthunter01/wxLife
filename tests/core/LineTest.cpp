#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Line.h"
#include "wxLife/core/Random.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

constexpr Coord kMin = std::numeric_limits<Coord>::min();
constexpr Coord kMax = std::numeric_limits<Coord>::max();

std::string text(CellPos p)
{
    return std::format("({},{})", p.x, p.y);
}

// The cells of the line from a to b, in visiting order, e.g. "(0,0) (1,0)".
std::string lineText(CellPos a, CellPos b)
{
    std::string cells;
    forEachCellOnLine(a, b, [&](CellPos p) { cells += (cells.empty() ? "" : " ") + text(p); });
    return cells;
}

std::vector<CellPos> line(CellPos a, CellPos b)
{
    std::vector<CellPos> cells;
    forEachCellOnLine(a, b, [&](CellPos p) { cells.push_back(p); });
    return cells;
}

constexpr std::int64_t magnitude(std::int64_t v)
{
    return v < 0 ? -v : v;
}

// Checks everything a line from a to b must satisfy; returns the first problem, or "" if there is
// none.
std::string lineProblem(CellPos a, CellPos b)
{
    const std::vector<CellPos> cells = line(a, b);
    const std::int64_t         dx    = std::int64_t{b.x} - a.x;
    const std::int64_t         dy    = std::int64_t{b.y} - a.y;
    const std::int64_t         steps = std::max(magnitude(dx), magnitude(dy));
    const std::string          where = " on " + text(a) + " to " + text(b);

    if (std::ssize(cells) != steps + 1)
    {
        return "wrong number of cells" + where;
    }
    if (cells.front() != a || cells.back() != b)
    {
        return "endpoints missing" + where;
    }
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        if (i > 0)
        {
            const std::int64_t stepX = std::int64_t{cells[i].x} - cells[i - 1].x;
            const std::int64_t stepY = std::int64_t{cells[i].y} - cells[i - 1].y;
            if (magnitude(stepX) > 1 || magnitude(stepY) > 1)
            {
                return "not 8-connected at " + text(cells[i]) + where;
            }
        }
        // Cell i is at most half a cell from the ideal point a + (b - a) * i / steps on both axes:
        // |offset * steps - d * i| <= steps / 2, in integers.
        const std::int64_t offsetX = std::int64_t{cells[i].x} - a.x;
        const std::int64_t offsetY = std::int64_t{cells[i].y} - a.y;
        const auto         i64     = static_cast<std::int64_t>(i);
        if (2 * magnitude((offsetX * steps) - (dx * i64)) > steps ||
            2 * magnitude((offsetY * steps) - (dy * i64)) > steps)
        {
            return "too far from the ideal line at " + text(cells[i]) + where;
        }
    }
    std::vector<CellPos> reversed = line(b, a);
    std::ranges::reverse(reversed);
    if (reversed != cells)
    {
        return "the reversed line visits other cells" + where;
    }
    return "";
}

TEST(LineTest, SinglePoint)
{
    EXPECT_EQ(lineText({3, -4}, {3, -4}), "(3,-4)");
}

TEST(LineTest, HorizontalVerticalAndDiagonal)
{
    EXPECT_EQ(lineText({0, 0}, {3, 0}), "(0,0) (1,0) (2,0) (3,0)");
    EXPECT_EQ(lineText({0, 0}, {0, -2}), "(0,0) (0,-1) (0,-2)");
    EXPECT_EQ(lineText({0, 0}, {-2, 2}), "(0,0) (-1,1) (-2,2)");
}

TEST(LineTest, ShallowAndSteep)
{
    // Offsets round to the nearest cell: 1/3 -> 0, 2/3 -> 1.
    EXPECT_EQ(lineText({0, 0}, {3, 1}), "(0,0) (1,0) (2,1) (3,1)");
    EXPECT_EQ(lineText({0, 0}, {1, 3}), "(0,0) (0,1) (1,2) (1,3)");
}

TEST(LineTest, TiesRoundTheSameWayInBothDirections)
{
    // (1, 0.5) is exactly between two cells; both directions must pick the same one.
    EXPECT_EQ(lineText({0, 0}, {2, 1}), "(0,0) (1,1) (2,1)");
    EXPECT_EQ(lineText({2, 1}, {0, 0}), "(2,1) (1,1) (0,0)");
}

TEST(LineTest, AllOctants)
{
    const auto ends = std::to_array<CellPos>(
        {{.x = 7, .y = 3},   {.x = 3, .y = 7},   {.x = -3, .y = 7},  {.x = -7, .y = 3},
         {.x = -7, .y = -3}, {.x = -3, .y = -7}, {.x = 3, .y = -7},  {.x = 7, .y = -3},
         {.x = 7, .y = 0},   {.x = 0, .y = 7},   {.x = -7, .y = 0},  {.x = 0, .y = -7},
         {.x = 7, .y = 7},   {.x = -7, .y = 7},  {.x = -7, .y = -7}, {.x = 7, .y = -7},
         {.x = 100, .y = 1}, {.x = 1, .y = 100}, {.x = 5, .y = 2},   {.x = 2, .y = 5},
         {.x = -6, .y = 4},  {.x = 4, .y = -6}});
    for (const CellPos end : ends)
    {
        EXPECT_EQ(lineProblem({0, 0}, end), "");
        EXPECT_EQ(lineProblem({-20, 13}, {end.x - 20, end.y + 13}), "");
    }
}

TEST(LineTest, RandomLines)
{
    SplitMix64 random(7);
    const auto coordinate = [&] { return static_cast<Coord>(random() % 201) - 100; };
    for (int i = 0; i < 2000; ++i)
    {
        const CellPos a{.x = coordinate(), .y = coordinate()};
        const CellPos b{.x = coordinate(), .y = coordinate()};
        ASSERT_EQ(lineProblem(a, b), "");
    }
}

TEST(LineTest, EndpointsNearTheIntegerLimitsDoNotOverflow)
{
    EXPECT_EQ(lineProblem({kMin, kMin}, {kMin + 5, kMin + 3}), "");
    EXPECT_EQ(lineProblem({kMax, kMax}, {kMax - 7, kMax - 2}), "");
    EXPECT_EQ(lineProblem({kMax, kMin}, {kMax - 3, kMin + 9}), "");
    EXPECT_EQ(lineProblem({kMin, kMax}, {kMin, kMax - 4}), "");
}

TEST(LineTest, LongLinesNeedWideArithmetic)
{
    // 2^21 steps with a 2^20 minor distance: the error terms need more than 32 bits.
    const Coord major = 1 << 21;
    const Coord minor = 1 << 20;
    EXPECT_EQ(lineProblem({kMax - major, kMin}, {kMax, kMin + minor}), "");
    EXPECT_EQ(lineProblem({kMin + 17, kMax}, {kMin + 17 + minor, kMax - major}), "");
}

}  // namespace
}  // namespace wxLife::core
