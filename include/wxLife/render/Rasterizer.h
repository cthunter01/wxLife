#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Grid.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Viewport.h"

namespace wxLife::render
{

/// Draws the part of a Grid visible through a Viewport. Cost is O(canvas pixels), independent of
/// world size.
class Rasterizer
{
public:
    /// Resizes `out` to viewport.canvasSize() and paints it.
    /// @pre viewport.worldExtent() == grid.extent()
    void render(const core::Grid& grid, const Viewport& viewport, const RenderStyle& style,
                PixelBuffer& out);

private:
    // Scratch rows rebuilt once per frame; kept as members so painting does not allocate.
    // A stamp is one cell's pixel run (cellSize × 3 bytes); the major ones end in a major grid-line
    // pixel. A grid row is a horizontal grid-line pixel row.
    std::vector<std::uint8_t> aliveStamp_;
    std::vector<std::uint8_t> deadStamp_;
    std::vector<std::uint8_t> aliveStampMajor_;
    std::vector<std::uint8_t> deadStampMajor_;
    std::vector<std::uint8_t> gridRow_;
    std::vector<std::uint8_t> gridRowMajor_;
};

/// Paints each ant's cell body in style.ant over a frame Rasterizer::render() has just drawn,
/// leaving the grid lines alone so an ant looks like a cell of its own colour. Ants outside the
/// view are skipped. A second pass, because only the ant automaton has ants and the cell loop stays
/// free of them.
/// @pre out.size() == viewport.canvasSize()
void drawAnts(std::span<const core::Ant> ants, const Viewport& viewport, const RenderStyle& style,
              PixelBuffer& out);

}  // namespace wxLife::render
