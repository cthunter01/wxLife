#include "wxLife/render/Viewport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include "wxLife/core/Types.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{

void Viewport::setWorldExtent(core::Extent world) noexcept
{
    world_ = world;
    clampOffset();
}

void Viewport::setCanvasSize(PixelSize canvas) noexcept
{
    canvas_ = {.width  = std::max<Pixel>(canvas.width, 0),
               .height = std::max<Pixel>(canvas.height, 0)};
    clampOffset();
}

core::Extent Viewport::worldExtent() const noexcept
{
    return world_;
}

PixelSize Viewport::canvasSize() const noexcept
{
    return canvas_;
}

int Viewport::cellSize() const noexcept
{
    return cellSize_;
}

PixelPoint Viewport::offset() const noexcept
{
    return offset_;
}

PixelSize Viewport::contentSize() const noexcept
{
    return {.width = Pixel{world_.width} * cellSize_, .height = Pixel{world_.height} * cellSize_};
}

void Viewport::setCellSize(int px, PixelPoint anchor) noexcept
{
    px = std::clamp(px, kMinCellSize, kMaxCellSize);
    if (px == cellSize_)
    {
        return;
    }
    auto& [runX, runY] = zoomRuns_;
    const PixelPoint wanted{.x = zoomedOffset(runX, offset_.x, anchor.x, cellSize_, px),
                            .y = zoomedOffset(runY, offset_.y, anchor.y, cellSize_, px)};
    offset_   = wanted;
    cellSize_ = px;
    clampOffset();

    // An axis that clamping moved has lost its point, so its run ends. The other axis keeps its
    // run.
    const auto recordOrEnd = [this](std::optional<ZoomRun>& run, Pixel offset, Pixel wantedOffset) {
        if (offset == wantedOffset)
        {
            run->offset   = offset;
            run->cellSize = cellSize_;
        }
        else
        {
            run.reset();
        }
    };
    recordOrEnd(runX, offset_.x, wanted.x);
    recordOrEnd(runY, offset_.y, wanted.y);
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
        std::ranges::lower_bound(kZoomSteps, cellSize_) - kZoomSteps.begin();
    const std::ptrdiff_t firstAbove =
        std::ranges::upper_bound(kZoomSteps, cellSize_) - kZoomSteps.begin();
    const std::ptrdiff_t target = steps > 0 ? firstAbove + (steps - 1) : firstNotBelow + steps;
    const std::ptrdiff_t index  = std::clamp(target, std::ptrdiff_t{0}, std::ssize(kZoomSteps) - 1);
    setCellSize(kZoomSteps.at(static_cast<std::size_t>(index)), anchor);
}

void Viewport::fitWorld() noexcept
{
    // Cell size that fits along one axis; an empty axis does not limit it.
    const auto fits = [](Pixel canvas, core::Coord side) {
        return side > 0 ? canvas / side : Pixel{kMaxCellSize};
    };
    const Pixel size =
        std::min(fits(canvas_.width, world_.width), fits(canvas_.height, world_.height));
    cellSize_ = static_cast<int>(std::clamp(size, Pixel{kMinCellSize}, Pixel{kMaxCellSize}));
    const PixelSize content = contentSize();

    offset_ = {.x = (content.width - canvas_.width) / 2,
               .y = (content.height - canvas_.height) / 2};
    clampOffset();
}

void Viewport::centerOn(core::CellPos cell) noexcept
{
    const auto axis = [this](core::Coord c, Pixel canvas) {
        return (Pixel{c} * cellSize_) + (cellSize_ / 2) - (canvas / 2);
    };
    offset_ = {.x = axis(cell.x, canvas_.width), .y = axis(cell.y, canvas_.height)};
    clampOffset();
}

void Viewport::panBy(Pixel dx, Pixel dy) noexcept
{
    offset_ = {.x = offset_.x + dx, .y = offset_.y + dy};
    clampOffset();
}

void Viewport::scrollTo(PixelPoint offset) noexcept
{
    offset_ = offset;
    clampOffset();
}

std::optional<core::CellPos> Viewport::cellAt(PixelPoint canvasPoint) const noexcept
{
    // Floor division: content pixels left of or above a centred world are negative.
    const std::int64_t x = core::floorDiv(offset_.x + canvasPoint.x, cellSize_);
    const std::int64_t y = core::floorDiv(offset_.y + canvasPoint.y, cellSize_);
    if (x < 0 || y < 0 || x >= world_.width || y >= world_.height)
    {
        return std::nullopt;
    }
    return core::CellPos{.x = static_cast<core::Coord>(x), .y = static_cast<core::Coord>(y)};
}

core::CellPos Viewport::cellAtClamped(PixelPoint canvasPoint) const noexcept
{
    const auto axis = [this](Pixel content, core::Coord side) {
        const std::int64_t cell = core::floorDiv(content, cellSize_);
        return static_cast<core::Coord>(std::clamp<std::int64_t>(cell, 0, std::max(side - 1, 0)));
    };
    return {.x = axis(offset_.x + canvasPoint.x, world_.width),
            .y = axis(offset_.y + canvasPoint.y, world_.height)};
}

PixelPoint Viewport::cellOrigin(core::CellPos cell) const noexcept
{
    return {.x = (Pixel{cell.x} * cellSize_) - offset_.x,
            .y = (Pixel{cell.y} * cellSize_) - offset_.y};
}

core::CellRect Viewport::visibleCells() const noexcept
{
    if (canvas_.width == 0 || canvas_.height == 0)
    {
        return {};
    }
    // Cells [first, end) along one axis whose pixels overlap [0, canvas).
    const auto axis = [this](Pixel offset, Pixel canvas, core::Coord side) {
        const auto clip = [side](std::int64_t cell) {
            return static_cast<core::Coord>(std::clamp<std::int64_t>(cell, 0, side));
        };
        return std::pair{clip(core::floorDiv(offset, cellSize_)),
                         clip(core::floorDiv(offset + canvas - 1, cellSize_) + 1)};
    };
    const auto [x0, x1] = axis(offset_.x, canvas_.width, world_.width);
    const auto [y0, y1] = axis(offset_.y, canvas_.height, world_.height);
    return {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

void Viewport::clampOffset() noexcept
{
    const auto axis = [](Pixel offset, Pixel content, Pixel canvas) {
        if (content <= canvas)
        {
            return -((canvas - content) / 2);  // centred, so the offset is zero or negative
        }
        return std::clamp(offset, Pixel{0}, content - canvas);
    };
    const PixelSize content = contentSize();

    offset_ = {.x = axis(offset_.x, content.width, canvas_.width),
               .y = axis(offset_.y, content.height, canvas_.height)};
}

}  // namespace wxLife::render
