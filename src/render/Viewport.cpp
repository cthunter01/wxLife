#include "wxLife/render/Viewport.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>

#include "wxLife/core/Types.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{

void Viewport::setWorldExtent(core::Extent world) noexcept
{
    m_world     = world;
    m_unbounded = false;
    clampOffset();
}

void Viewport::setUnbounded() noexcept
{
    m_world     = {};
    m_unbounded = true;
    clampOffset();
}

bool Viewport::unbounded() const noexcept
{
    return m_unbounded;
}

void Viewport::setCanvasSize(PixelSize canvas) noexcept
{
    m_canvas = {.width  = std::max<Pixel>(canvas.width, 0),
                .height = std::max<Pixel>(canvas.height, 0)};
    clampOffset();
}

core::Extent Viewport::worldExtent() const noexcept
{
    return m_world;
}

PixelSize Viewport::canvasSize() const noexcept
{
    return m_canvas;
}

int Viewport::cellSize() const noexcept
{
    return m_cellSize;
}

PixelPoint Viewport::offset() const noexcept
{
    return m_offset;
}

PixelSize Viewport::contentSize() const noexcept
{
    return {.width  = Pixel{m_world.width} * m_cellSize,
            .height = Pixel{m_world.height} * m_cellSize};
}

void Viewport::setCellSize(int px, PixelPoint anchor) noexcept
{
    px = std::clamp(px, kMinCellSize, kMaxCellSize);
    if (px == m_cellSize)
    {
        return;
    }
    auto& [runX, runY] = m_zoomRuns;
    const PixelPoint wanted{.x = zoomedOffset(runX, m_offset.x, anchor.x, m_cellSize, px),
                            .y = zoomedOffset(runY, m_offset.y, anchor.y, m_cellSize, px)};
    m_offset   = wanted;
    m_cellSize = px;
    clampOffset();

    // An axis that clamping moved has lost its point, so its run ends. The other axis keeps its
    // run.
    const auto recordOrEnd = [this](std::optional<ZoomRun>& run, Pixel offset, Pixel wantedOffset) {
        if (offset == wantedOffset)
        {
            run->offset   = offset;
            run->cellSize = m_cellSize;
        }
        else
        {
            run.reset();
        }
    };
    recordOrEnd(runX, m_offset.x, wanted.x);
    recordOrEnd(runY, m_offset.y, wanted.y);
}

Pixel Viewport::zoomedOffset(std::optional<ZoomRun>& run, Pixel offset, Pixel anchor, int cellSize,
                             int px) noexcept
{
    // The run goes on if its last zoom used this anchor and nothing has moved this axis since.
    if (!run || run->anchor != anchor || run->offset != offset || run->cellSize != cellSize)
    {
        run = ZoomRun{.anchor = anchor, .pixel = offset + anchor, .pixelScale = cellSize};
    }
    // Integers only. The centre of content pixel c at cell size s lies (2c + 1) / 2s cells into the
    // content, so at size px it falls into content pixel ⌊(2c + 1) × px / 2s⌋, which goes under the
    // anchor. That pixel is in the same cell as c. Floor division, because the anchor may be left
    // of the world.
    return core::floorDiv(((2 * run->pixel) + 1) * px, 2 * Pixel{run->pixelScale}) - anchor;
}

void Viewport::zoomBy(int steps, PixelPoint anchor) noexcept
{
    if (steps == 0)
    {
        return;
    }
    // Count from the current size, so a size between two entries moves to its neighbour
    // (7 px zooms out to 6 and in to 8) instead of first snapping to the nearest entry.
    const std::ptrdiff_t firstNotBelow =
        std::ranges::lower_bound(kZoomSteps, m_cellSize) - kZoomSteps.begin();
    const std::ptrdiff_t firstAbove =
        std::ranges::upper_bound(kZoomSteps, m_cellSize) - kZoomSteps.begin();
    const std::ptrdiff_t target = steps > 0 ? firstAbove + (steps - 1) : firstNotBelow + steps;
    const std::ptrdiff_t index  = std::clamp(target, std::ptrdiff_t{0}, std::ssize(kZoomSteps) - 1);
    setCellSize(kZoomSteps.at(static_cast<std::size_t>(index)), anchor);
}

void Viewport::fitWorld() noexcept
{
    if (!m_unbounded)
    {
        fitCells({.x0 = 0, .y0 = 0, .x1 = m_world.width, .y1 = m_world.height});
    }
}

void Viewport::fitCells(core::UniverseRect cells) noexcept
{
    // Cell size that fits along one axis; an empty axis does not limit it.
    const auto fits = [](Pixel canvas, core::UniverseCoord side) {
        return side > 0 ? canvas / side : Pixel{kMaxCellSize};
    };
    const Pixel size = std::min(fits(m_canvas.width, cells.x1 - cells.x0),
                                fits(m_canvas.height, cells.y1 - cells.y0));
    m_cellSize       = static_cast<int>(std::clamp(size, Pixel{kMinCellSize}, Pixel{kMaxCellSize}));

    // The centre of the cells under the centre of the canvas. Cells beyond the reach are pulled
    // in first, so no product overflows; clamping would stop the view there anyway.
    const core::UniverseCoord reach = kUnboundedReach / m_cellSize;
    const auto axis = [&](core::UniverseCoord from, core::UniverseCoord to, Pixel canvas) {
        const core::UniverseCoord lo = std::clamp(from, -reach, reach);
        const core::UniverseCoord hi = std::clamp(to, -reach, reach);
        return (((Pixel{lo} + hi) * m_cellSize) - canvas) / 2;
    };
    m_offset = {.x = axis(cells.x0, cells.x1, m_canvas.width),
                .y = axis(cells.y0, cells.y1, m_canvas.height)};
    clampOffset();
}

void Viewport::centerOn(core::UniversePos cell) noexcept
{
    const core::UniverseCoord reach = kUnboundedReach / m_cellSize;
    const auto                axis  = [&](core::UniverseCoord c, Pixel canvas) {
        return (std::clamp(c, -reach, reach) * m_cellSize) + (m_cellSize / 2) - (canvas / 2);
    };
    m_offset = {.x = axis(cell.x, m_canvas.width), .y = axis(cell.y, m_canvas.height)};
    clampOffset();
}

void Viewport::panBy(Pixel dx, Pixel dy) noexcept
{
    m_offset = {.x = m_offset.x + dx, .y = m_offset.y + dy};
    clampOffset();
}

void Viewport::scrollTo(PixelPoint offset) noexcept
{
    m_offset = offset;
    clampOffset();
}

std::optional<core::UniversePos> Viewport::cellAt(PixelPoint canvasPoint) const noexcept
{
    // Floor division: content pixels left of or above a centred world are negative.
    const core::UniversePos cell{.x = core::floorDiv(m_offset.x + canvasPoint.x, m_cellSize),
                                 .y = core::floorDiv(m_offset.y + canvasPoint.y, m_cellSize)};
    if (!m_unbounded &&
        (cell.x < 0 || cell.y < 0 || cell.x >= m_world.width || cell.y >= m_world.height))
    {
        return std::nullopt;
    }
    return cell;
}

core::UniversePos Viewport::cellAtClamped(PixelPoint canvasPoint) const noexcept
{
    const auto axis = [this](Pixel content, core::Coord side) {
        const core::UniverseCoord cell = core::floorDiv(content, m_cellSize);
        return m_unbounded ? cell : std::clamp<core::UniverseCoord>(cell, 0, std::max(side - 1, 0));
    };
    return {.x = axis(m_offset.x + canvasPoint.x, m_world.width),
            .y = axis(m_offset.y + canvasPoint.y, m_world.height)};
}

PixelPoint Viewport::cellOrigin(core::UniversePos cell) const noexcept
{
    return {.x = (Pixel{cell.x} * m_cellSize) - m_offset.x,
            .y = (Pixel{cell.y} * m_cellSize) - m_offset.y};
}

core::UniverseRect Viewport::visibleCells() const noexcept
{
    if (m_canvas.width == 0 || m_canvas.height == 0)
    {
        return {};
    }
    // Cells [first, end) along one axis whose pixels overlap [0, canvas).
    const auto axis = [this](Pixel offset, Pixel canvas, core::Coord side) {
        const auto clip = [this, side](core::UniverseCoord cell) {
            return m_unbounded ? cell : std::clamp<core::UniverseCoord>(cell, 0, side);
        };
        return std::pair{clip(core::floorDiv(offset, m_cellSize)),
                         clip(core::floorDiv(offset + canvas - 1, m_cellSize) + 1)};
    };
    const auto [x0, x1] = axis(m_offset.x, m_canvas.width, m_world.width);
    const auto [y0, y1] = axis(m_offset.y, m_canvas.height, m_world.height);
    return {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

void Viewport::clampOffset() noexcept
{
    if (m_unbounded)
    {
        m_offset = {
            .x = std::clamp(m_offset.x, -kUnboundedReach, kUnboundedReach - m_canvas.width),
            .y = std::clamp(m_offset.y, -kUnboundedReach, kUnboundedReach - m_canvas.height)};
        return;
    }
    const auto axis = [](Pixel offset, Pixel content, Pixel canvas) {
        if (content <= canvas)
        {
            return -((canvas - content) / 2);  // centred, so the offset is zero or negative
        }
        return std::clamp(offset, Pixel{0}, content - canvas);
    };
    const PixelSize content = contentSize();

    m_offset = {.x = axis(m_offset.x, content.width, m_canvas.width),
                .y = axis(m_offset.y, content.height, m_canvas.height)};
}

}  // namespace wxLife::render
