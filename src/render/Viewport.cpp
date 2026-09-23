#include "wxLife/render/Viewport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "wxLife/core/Format.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{
namespace
{

/// toString() spells out blocks up to 2^10 cells wide, and writes powers of two beyond.
constexpr unsigned kLongestExactShrink = 10;

/// How far an unbounded view reaches from (0, 0), in content pixels: kUnboundedReach, or less once
/// the universe ends nearer.
Pixel unboundedReach(Scale scale) noexcept
{
    return scale.zoomedOut()
               ? std::min(Viewport::kUnboundedReach, core::kUniverseRadius >> scale.shrink)
               : Viewport::kUnboundedReach;
}

/// The first cell that content pixel `pixel` shows.
core::UniverseCoord firstCellAt(Pixel pixel, Scale scale) noexcept
{
    return scale.zoomedOut() ? pixel * scale.cellsPerPixel()
                             : core::floorDiv(pixel, scale.cellSize);
}

/// The content pixel that shows `cell`; its first one when cells are larger than a pixel.
Pixel pixelOf(core::UniverseCoord cell, Scale scale) noexcept
{
    return scale.zoomedOut() ? core::floorDiv(cell, scale.cellsPerPixel()) : cell * scale.cellSize;
}

/// Content pixels that show cells [0, cells); a partly filled last pixel counts. @pre cells >= 0
Pixel lengthOf(core::UniverseCoord cells, Scale scale) noexcept
{
    return scale.zoomedOut() ? (cells + scale.cellsPerPixel() - 1) / scale.cellsPerPixel()
                             : cells * scale.cellSize;
}

}  // namespace

std::string toString(Scale scale)
{
    if (!scale.zoomedOut())
    {
        return std::format("{} px", scale.cellSize);
    }
    if (scale.shrink <= kLongestExactShrink)
    {
        return std::format("1/{} px",
                           core::formatCount(static_cast<std::uint64_t>(scale.cellsPerPixel())));
    }
    return std::format("1/2^{} px", scale.shrink);
}

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

Scale Viewport::scale() const noexcept
{
    return m_scale;
}

PixelPoint Viewport::offset() const noexcept
{
    return m_offset;
}

PixelSize Viewport::contentSize() const noexcept
{
    return {.width = lengthOf(m_world.width, m_scale), .height = lengthOf(m_world.height, m_scale)};
}

unsigned Viewport::maxShrink() const noexcept
{
    const auto fits = [this](unsigned shrink) {
        const Scale scale{.shrink = shrink};
        const auto  along = [&](core::Coord side, Pixel canvas) {
            const auto [first, end] = contentSpan(side, scale);
            return end - first <= std::max<Pixel>(canvas, 1);
        };
        return along(m_world.width, m_canvas.width) && along(m_world.height, m_canvas.height);
    };
    unsigned shrink = 0;
    while (shrink < kMaxShrink && !fits(shrink))
    {
        ++shrink;
    }
    return shrink;
}

void Viewport::setScale(Scale scale, PixelPoint anchor) noexcept
{
    // A view zoomed out beyond the limit (the canvas has grown since) may zoom in, but no further
    // out.
    scale = scale.zoomedOut()
                ? Scale{.shrink = std::min(scale.shrink, std::max(maxShrink(), m_scale.shrink))}
                : Scale{.cellSize = std::clamp(scale.cellSize, kMinCellSize, kMaxCellSize)};
    if (scale == m_scale)
    {
        return;
    }
    auto& [runX, runY] = m_zoomRuns;
    const PixelPoint wanted{.x = zoomedOffset(runX, m_offset.x, anchor.x, m_scale, scale),
                            .y = zoomedOffset(runY, m_offset.y, anchor.y, m_scale, scale)};
    m_offset = wanted;
    m_scale  = scale;
    clampOffset();

    // An axis that clamping moved has lost its point, so its run ends. The other axis keeps its
    // run.
    const auto recordOrEnd = [this](std::optional<ZoomRun>& run, Pixel offset, Pixel wantedOffset) {
        if (offset == wantedOffset)
        {
            run->offset = offset;
            run->scale  = m_scale;
        }
        else
        {
            run.reset();
        }
    };
    recordOrEnd(runX, m_offset.x, wanted.x);
    recordOrEnd(runY, m_offset.y, wanted.y);
}

void Viewport::setCellSize(int px, PixelPoint anchor) noexcept
{
    setScale({.cellSize = px}, anchor);
}

Pixel Viewport::zoomedOffset(std::optional<ZoomRun>& run, Pixel offset, Pixel anchor, Scale from,
                             Scale to) noexcept
{
    // The run goes on if its last zoom used this anchor and nothing has moved this axis since.
    if (!run || run->anchor != anchor || run->offset != offset || run->scale != from)
    {
        // The point is the centre of the anchor's content pixel.
        const Pixel pixel = offset + anchor;
        run               = ZoomRun{.anchor = anchor};
        if (from.zoomedOut())
        {
            // A pixel is an even number of cells wide, so its centre is the corner of a cell.
            run->cell = (pixel * from.cellsPerPixel()) + (from.cellsPerPixel() / 2);
        }
        else
        {
            // (2 × (pixel mod size) + 1) / (2 × size) of a cell into the cell under the pixel.
            run->cell        = core::floorDiv(pixel, from.cellSize);
            run->numerator   = (2 * (pixel - (run->cell * from.cellSize))) + 1;
            run->denominator = 2 * Pixel{from.cellSize};
        }
    }
    // Integers only, so the point goes under the anchor exactly: the content pixel that holds it
    // goes there. Zoomed out, the fraction never moves the point into another pixel, because
    // pixels start at whole cells. Zoomed in, a point beyond the reach is pulled in first so no
    // product overflows; clamping moves the view anyway then.
    if (to.zoomedOut())
    {
        return core::floorDiv(run->cell, to.cellsPerPixel()) - anchor;
    }
    const core::UniverseCoord cell = std::clamp(run->cell, -kUnboundedReach, kUnboundedReach);
    return (cell * to.cellSize) + ((run->numerator * to.cellSize) / run->denominator) - anchor;
}

void Viewport::zoomBy(int steps, PixelPoint anchor) noexcept
{
    if (steps == 0)
    {
        return;
    }
    // Places on the ladder: the kZoomSteps entries count up from 0, the shrink levels down from -1.
    std::ptrdiff_t target = 0;
    if (m_scale.zoomedOut())
    {
        target = steps - static_cast<std::ptrdiff_t>(m_scale.shrink);
    }
    else
    {
        // Count from the current size, so a size between two entries moves to its neighbour
        // (7 px zooms out to 6 and in to 8) instead of first snapping to the nearest entry.
        const std::ptrdiff_t firstNotBelow =
            std::ranges::lower_bound(kZoomSteps, m_scale.cellSize) - kZoomSteps.begin();
        const std::ptrdiff_t firstAbove =
            std::ranges::upper_bound(kZoomSteps, m_scale.cellSize) - kZoomSteps.begin();
        target = steps > 0 ? firstAbove + (steps - 1) : firstNotBelow + steps;
    }
    target =
        std::clamp(target, -static_cast<std::ptrdiff_t>(kMaxShrink), std::ssize(kZoomSteps) - 1);
    if (target >= 0)
    {
        setScale({.cellSize = kZoomSteps.at(static_cast<std::size_t>(target))}, anchor);
    }
    else
    {
        setScale({.shrink = static_cast<unsigned>(-target)}, anchor);
    }
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
    const Pixel size       = std::min(fits(m_canvas.width, cells.x1 - cells.x0),
                                      fits(m_canvas.height, cells.y1 - cells.y0));
    const bool  empty      = m_canvas.width == 0 || m_canvas.height == 0;
    const auto  inUniverse = [](core::UniverseCoord c) {
        return std::clamp(c, -core::kUniverseRadius, core::kUniverseRadius);
    };
    if (size >= kMinCellSize || empty || maxShrink() == 0)
    {
        m_scale = {.cellSize = static_cast<int>(
                       std::clamp(size, Pixel{kMinCellSize}, Pixel{kMaxCellSize}))};

        // The centre of the cells under the centre of the canvas. Cells beyond the reach are
        // pulled in first, so no product overflows; clamping would stop the view there anyway.
        const core::UniverseCoord reach = kUnboundedReach / m_scale.cellSize;
        const auto axis = [&](core::UniverseCoord from, core::UniverseCoord to, Pixel canvas) {
            const core::UniverseCoord lo = std::clamp(from, -reach, reach);
            const core::UniverseCoord hi = std::clamp(to, -reach, reach);
            return (((Pixel{lo} + hi) * m_scale.cellSize) - canvas) / 2;
        };
        m_offset = {.x = axis(cells.x0, cells.x1, m_canvas.width),
                    .y = axis(cells.y0, cells.y1, m_canvas.height)};
    }
    else
    {
        // Below 1 px: the smallest shrink whose pixels, aligned as they are, show every cell.
        const auto pixels = [&](core::UniverseCoord from, core::UniverseCoord to, Pixel perPixel) {
            const Pixel first = core::floorDiv(inUniverse(from), perPixel);
            const Pixel end = to > from ? core::floorDiv(inUniverse(to) - 1, perPixel) + 1 : first;
            return std::pair{first, end};
        };
        const unsigned limit    = maxShrink();
        unsigned       shrink   = 1;
        const auto     tooLarge = [&](unsigned candidate) {
            const Pixel perPixel     = Pixel{1} << candidate;
            const auto [left, right] = pixels(cells.x0, cells.x1, perPixel);
            const auto [top, bottom] = pixels(cells.y0, cells.y1, perPixel);
            return right - left > m_canvas.width || bottom - top > m_canvas.height;
        };
        while (shrink < limit && tooLarge(shrink))
        {
            ++shrink;
        }
        m_scale = {.shrink = shrink};

        // The centre of those pixels under the centre of the canvas.
        const auto axis = [&](core::UniverseCoord from, core::UniverseCoord to, Pixel canvas) {
            const auto [first, end] = pixels(from, to, m_scale.cellsPerPixel());
            return core::floorDiv(first + end - canvas, 2);
        };
        m_offset = {.x = axis(cells.x0, cells.x1, m_canvas.width),
                    .y = axis(cells.y0, cells.y1, m_canvas.height)};
    }
    clampOffset();
}

void Viewport::centerOn(core::UniversePos cell) noexcept
{
    const auto axis = [this](core::UniverseCoord c, Pixel canvas) {
        if (m_scale.zoomedOut())
        {
            const core::UniverseCoord inside =
                std::clamp(c, -core::kUniverseRadius, core::kUniverseRadius);
            return core::floorDiv(inside, m_scale.cellsPerPixel()) - (canvas / 2);
        }
        const core::UniverseCoord reach = kUnboundedReach / m_scale.cellSize;
        return (std::clamp(c, -reach, reach) * m_scale.cellSize) + (m_scale.cellSize / 2) -
               (canvas / 2);
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
    const auto axis = [this](Pixel pixel, core::Coord side) -> std::optional<core::UniverseCoord> {
        const auto [first, end] = contentSpan(side, m_scale);
        if (pixel < first || pixel >= end)
        {
            return std::nullopt;
        }
        return firstCellAt(pixel, m_scale);
    };
    const std::optional<core::UniverseCoord> x = axis(m_offset.x + canvasPoint.x, m_world.width);
    const std::optional<core::UniverseCoord> y = axis(m_offset.y + canvasPoint.y, m_world.height);
    if (!x || !y)
    {
        return std::nullopt;
    }
    return core::UniversePos{.x = *x, .y = *y};
}

core::UniversePos Viewport::cellAtClamped(PixelPoint canvasPoint) const noexcept
{
    const auto axis = [this](Pixel pixel, core::Coord side) {
        const auto [first, end] = contentSpan(side, m_scale);
        return firstCellAt(std::clamp(pixel, first, std::max(first, end - 1)), m_scale);
    };
    return {.x = axis(m_offset.x + canvasPoint.x, m_world.width),
            .y = axis(m_offset.y + canvasPoint.y, m_world.height)};
}

PixelPoint Viewport::cellOrigin(core::UniversePos cell) const noexcept
{
    return {.x = pixelOf(cell.x, m_scale) - m_offset.x, .y = pixelOf(cell.y, m_scale) - m_offset.y};
}

core::UniverseRect Viewport::visibleCells() const noexcept
{
    if (m_canvas.width == 0 || m_canvas.height == 0)
    {
        return {};
    }
    // Cells [first, end) along one axis whose pixels overlap [0, canvas), within the world.
    const auto axis = [this](Pixel offset, Pixel canvas, core::Coord side) {
        const auto [first, end] = contentSpan(side, m_scale);
        const Pixel from        = std::max(offset, first);
        const Pixel to          = std::min(offset + canvas, end);
        if (m_scale.zoomedOut())
        {
            const core::UniverseCoord last = to * m_scale.cellsPerPixel();
            return std::pair{from * m_scale.cellsPerPixel(),
                             m_unbounded ? last : std::min<core::UniverseCoord>(last, side)};
        }
        return std::pair{core::floorDiv(from, m_scale.cellSize),
                         core::floorDiv(to - 1, m_scale.cellSize) + 1};
    };
    const auto [x0, x1] = axis(m_offset.x, m_canvas.width, m_world.width);
    const auto [y0, y1] = axis(m_offset.y, m_canvas.height, m_world.height);
    return {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

std::pair<Pixel, Pixel> Viewport::contentSpan(core::Coord side, Scale scale) const noexcept
{
    if (m_unbounded)
    {
        const Pixel reach = unboundedReach(scale);
        return {-reach, reach};
    }
    return {0, lengthOf(side, scale)};
}

void Viewport::clampOffset() noexcept
{
    const auto axis = [this](Pixel offset, Pixel canvas, core::Coord side) {
        const auto [first, end] = contentSpan(side, m_scale);
        if (end - first <= canvas)
        {
            return first - ((canvas - (end - first)) / 2);  // centred
        }
        return std::clamp(offset, first, end - canvas);
    };
    m_offset = {.x = axis(m_offset.x, m_canvas.width, m_world.width),
                .y = axis(m_offset.y, m_canvas.height, m_world.height)};
}

}  // namespace wxLife::render
