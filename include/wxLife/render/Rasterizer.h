#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Viewport.h"

namespace wxLife::render
{

/// Draws the part of a world visible through a Viewport. From 1 px per cell on, the cost is
/// O(canvas pixels), independent of world size. Below 1 px, each pixel shows a block of cells and
/// is alive if any of them is; a grid then costs a read of every visible cell (on several threads),
/// a plane one visit per lit pixel.
class Rasterizer
{
public:
    /// Resizes `out` to viewport.canvasSize() and paints it.
    /// @pre viewport.worldExtent() == grid.extent()
    void render(const core::Grid& grid, const Viewport& viewport, const RenderStyle& style,
                PixelBuffer& out);
    /// The same for an unbounded plane. The cells (or blocks) the view shows are gathered into a
    /// grid of their own first (forEachBlock() skips the empty ones), so from there on the drawing
    /// is the same as for a fixed-size world. @pre viewport.unbounded()
    void render(const core::HashLife& plane, const Viewport& viewport, const RenderStyle& style,
                PixelBuffer& out);

private:
    /// m_window becomes an all-dead grid the size of `cells`.
    void resetWindow(core::UniverseRect cells);
    /// Paints `grid`, whose cell (0, 0) is world cell `origin`, as `viewport` shows the world.
    /// @pre the grid holds every cell the viewport shows
    void paint(const core::Grid& grid, core::UniversePos origin, const Viewport& viewport,
               const RenderStyle& style, PixelBuffer& out);

    /// The visible part of a plane, or below 1 px the visible blocks; reused while its size stays.
    core::Grid                           m_window;
    std::vector<std::vector<core::Cell>> m_bandRows;  ///< shrinkGrid()'s scratch rows
    // Scratch rows rebuilt once per frame; kept as members so painting does not allocate.
    // A stamp is one cell's pixel run (cellSize × 3 bytes); the major ones end in a major grid-line
    // pixel. A grid row is a horizontal grid-line pixel row.
    std::vector<std::uint8_t> m_aliveStamp;
    std::vector<std::uint8_t> m_deadStamp;
    std::vector<std::uint8_t> m_aliveStampMajor;
    std::vector<std::uint8_t> m_deadStampMajor;
    std::vector<std::uint8_t> m_gridRow;
    std::vector<std::uint8_t> m_gridRowMajor;
};

/// Paints each ant's cell body in style.ant over a frame Rasterizer::render() has just drawn,
/// leaving the grid lines alone so an ant looks like a cell of its own colour. Ants outside the
/// view are skipped. A second pass, because only the ant automaton has ants and the cell loop stays
/// free of them.
/// @pre out.size() == viewport.canvasSize()
void drawAnts(std::span<const core::Ant> ants, const Viewport& viewport, const RenderStyle& style,
              PixelBuffer& out);

}  // namespace wxLife::render
