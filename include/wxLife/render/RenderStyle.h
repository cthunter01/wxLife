#pragma once

#include "wxLife/render/Types.h"

namespace wxLife::render
{

/// Colours and grid-line policy for the rasterizer.
struct RenderStyle
{
    Rgb  alive;
    Rgb  dead;
    Rgb  gridLine;
    Rgb  gridLineMajor;
    Rgb  outside;  ///< Canvas area beyond the world's edge.
    Rgb  ant;      ///< The cell an ant stands on, painted over that cell's own colour.
    bool showGrid           = true;
    int  minCellSizeForGrid = 5;  ///< Below this, lines would hide the cells.
    /// Every n-th line uses gridLineMajor, also where it crosses a minor line. 0: no major lines.
    int majorGridEvery = 10;

    [[nodiscard]] constexpr bool gridVisibleAt(int cellSize) const noexcept
    {
        return showGrid && cellSize >= minCellSizeForGrid;
    }
};

[[nodiscard]] constexpr RenderStyle darkStyle() noexcept
{
    return {.alive{.r = 0xF2, .g = 0xC1, .b = 0x4E},
            .dead{.r = 0x16, .g = 0x1A, .b = 0x20},
            .gridLine{.r = 0x26, .g = 0x2B, .b = 0x33},
            .gridLineMajor{.r = 0x3A, .g = 0x41, .b = 0x4D},
            .outside{.r = 0x0B, .g = 0x0C, .b = 0x0E},
            .ant{.r = 0x4E, .g = 0xC9, .b = 0xF2}};
}

[[nodiscard]] constexpr RenderStyle lightStyle() noexcept
{
    return {.alive{.r = 0x1F, .g = 0x29, .b = 0x37},
            .dead{.r = 0xFA, .g = 0xFA, .b = 0xF7},
            .gridLine{.r = 0xE3, .g = 0xE3, .b = 0xDE},
            .gridLineMajor{.r = 0xC4, .g = 0xC4, .b = 0xBC},
            .outside{.r = 0xD5, .g = 0xD8, .b = 0xDC},
            .ant{.r = 0xC6, .g = 0x28, .b = 0x28}};
}

}  // namespace wxLife::render
