/// @file
/// Test helpers that turn small grids into text and back, so expected patterns can be read at
/// a glance.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "wxLife/core/Grid.h"
#include "wxLife/core/Types.h"

namespace wxLife::test
{

/// Builds a grid from rows of 'O' (alive) and '.' (dead), top row first:
/// gridFromAscii({".O.", "..O"}).
/// @throws std::invalid_argument if the rows differ in length or contain another character.
[[nodiscard]] inline core::Grid gridFromAscii(std::initializer_list<std::string_view> rows)
{
    const std::size_t width = rows.size() == 0 ? 0 : rows.begin()->size();

    core::Grid grid({.width  = static_cast<core::Coord>(width),
                     .height = static_cast<core::Coord>(rows.size())});

    core::Coord y = 0;
    for (const std::string_view row : rows)
    {
        if (row.size() != width)
        {
            throw std::invalid_argument("gridFromAscii: rows differ in length");
        }
        for (std::size_t x = 0; x < width; ++x)
        {
            if (row[x] != 'O' && row[x] != '.')
            {
                throw std::invalid_argument("gridFromAscii: use 'O' and '.' only");
            }
            grid.set({.x = static_cast<core::Coord>(x), .y = y},
                     row[x] == 'O' ? core::kAlive : core::kDead);
        }
        ++y;
    }
    return grid;
}

/// The interior cells as 'O' and '.', one line per row, each ending in '\n'.
[[nodiscard]] inline std::string toAscii(const core::Grid& grid)
{
    const core::Extent extent = grid.extent();
    std::string        text;
    text.reserve(static_cast<std::size_t>(extent.cellCount() + extent.height));
    for (core::Coord y = 0; y < extent.height; ++y)
    {
        for (const core::Cell cell : grid.row(y))
        {
            text += cell != core::kDead ? 'O' : '.';
        }
        text += '\n';
    }
    return text;
}

}  // namespace wxLife::test

namespace wxLife::core
{

/// Lets GoogleTest print a Grid as ASCII art when an EXPECT_EQ on two grids fails (found by ADL).
inline void PrintTo(const Grid& grid, std::ostream* os)
{
    *os << '\n' << test::toAscii(grid);
}

}  // namespace wxLife::core
