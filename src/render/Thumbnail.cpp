#include "wxLife/render/Thumbnail.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{

namespace
{

// Paints the pixels [left, right) × [top, bottom), clipped to the buffer.
void fillRect(PixelBuffer& out, Pixel left, Pixel top, Pixel right, Pixel bottom, Rgb color)
{
    left   = std::max<Pixel>(left, 0);
    top    = std::max<Pixel>(top, 0);
    right  = std::min(right, out.size().width);
    bottom = std::min(bottom, out.size().height);
    for (Pixel y = top; y < bottom; ++y)
    {
        const std::span<std::uint8_t> row = out.row(y);
        for (Pixel x = left; x < right; ++x)
        {
            const auto i = static_cast<std::size_t>(x) * PixelBuffer::kBytesPerPixel;
            row[i]       = color.r;
            row[i + 1]   = color.g;
            row[i + 2]   = color.b;
        }
    }
}

}  // namespace

void drawThumbnail(std::span<const core::CellPos> cells, std::span<const core::Ant> ants,
                   core::Extent extent, PixelSize maxSize, const RenderStyle& style,
                   PixelBuffer& out)
{
    if (extent.width <= 0 || extent.height <= 0 || maxSize.width <= 0 || maxSize.height <= 0)
    {
        out.resize({});
        return;
    }

    // Either `scale` pixels per cell, or scaled down to `size`, one pixel for several cells.
    const bool shrink = extent.width > maxSize.width || extent.height > maxSize.height;
    Pixel      scale  = 1;
    PixelSize  size{};
    if (!shrink)
    {
        scale = std::min({maxSize.width / extent.width, maxSize.height / extent.height,
                          Pixel{kMaxThumbnailCellSize}});
        size  = {.width = Pixel{extent.width} * scale, .height = Pixel{extent.height} * scale};
    }
    else if (maxSize.width * extent.height <= maxSize.height * extent.width)  // width decides
    {
        size = {.width  = maxSize.width,
                .height = std::max<Pixel>(1, extent.height * maxSize.width / extent.width)};
    }
    else
    {
        size = {.width  = std::max<Pixel>(1, extent.width * maxSize.height / extent.height),
                .height = maxSize.height};
    }
    // The first pixel of a cell's column and row.
    const auto left = [&](core::Coord x) {
        return shrink ? Pixel{x} * size.width / extent.width : Pixel{x} * scale;
    };
    const auto top = [&](core::Coord y) {
        return shrink ? Pixel{y} * size.height / extent.height : Pixel{y} * scale;
    };

    out.resize(size);
    out.fill(style.dead);
    for (const core::CellPos cell : cells)
    {
        assert(extent.contains(cell));
        const Pixel x = left(cell.x);
        const Pixel y = top(cell.y);
        fillRect(out, x, y, x + scale, y + scale, style.alive);
    }

    // An ant is a square centred on its cell, large enough to see.
    const Pixel antSize = std::max(scale, Pixel{kMinThumbnailAntSize});
    for (const core::Ant& ant : ants)
    {
        assert(extent.contains(ant.position));
        const Pixel x = left(ant.position.x) + (scale / 2) - (antSize / 2);
        const Pixel y = top(ant.position.y) + (scale / 2) - (antSize / 2);
        fillRect(out, x, y, x + antSize, y + antSize, style.ant);
    }
}

void drawThumbnail(const core::HashLife& plane, core::UniverseRect area, PixelSize maxSize,
                   const RenderStyle& style, PixelBuffer& out)
{
    if (area.empty() || maxSize.width <= 0 || maxSize.height <= 0)
    {
        out.resize({});
        return;
    }
    // The blocks of 2^level cells that hold `area`, aligned as the plane aligns them.
    const auto blocksAt = [&area](unsigned level) {
        const std::int64_t side = std::int64_t{1} << level;
        return core::UniverseRect{.x0 = core::floorDiv(area.x0, side),
                                  .y0 = core::floorDiv(area.y0, side),
                                  .x1 = core::floorDiv(area.x1 - 1, side) + 1,
                                  .y1 = core::floorDiv(area.y1 - 1, side) + 1};
    };
    unsigned           level  = 0;
    core::UniverseRect blocks = area;
    while (level < core::HashLife::kMaxLevel &&
           (blocks.x1 - blocks.x0 > maxSize.width || blocks.y1 - blocks.y0 > maxSize.height))
    {
        blocks = blocksAt(++level);
    }
    const std::int64_t         side = std::int64_t{1} << level;
    std::vector<core::CellPos> cells;
    plane.forEachBlock(area, level, [&](core::UniversePos corner) {
        cells.push_back(
            {.x = static_cast<core::Coord>(core::floorDiv(corner.x, side) - blocks.x0),
             .y = static_cast<core::Coord>(core::floorDiv(corner.y, side) - blocks.y0)});
    });
    drawThumbnail(cells, {},
                  {.width  = static_cast<core::Coord>(blocks.x1 - blocks.x0),
                   .height = static_cast<core::Coord>(blocks.y1 - blocks.y0)},
                  maxSize, style, out);
}

}  // namespace wxLife::render
