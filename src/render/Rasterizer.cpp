#include "wxLife/render/Rasterizer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Types.h"
#include "wxLife/render/PixelBuffer.h"
#include "wxLife/render/RenderStyle.h"
#include "wxLife/render/Types.h"
#include "wxLife/render/Viewport.h"

namespace wxLife::render
{
namespace
{

using Bytes      = std::span<std::uint8_t>;
using ConstBytes = std::span<const std::uint8_t>;

constexpr std::size_t kPixelBytes = PixelBuffer::kBytesPerPixel;

// The bytes of pixels [from, to) in a run of RGB pixels. A whole frame is one long run, row
// after row.
template <typename Byte>
std::span<Byte> pixels(std::span<Byte> run, Pixel from, Pixel to) noexcept
{
    return run.subspan(static_cast<std::size_t>(from) * kPixelBytes,
                       static_cast<std::size_t>(to - from) * kPixelBytes);
}

void setPixel(Bytes run, Pixel x, Rgb color) noexcept
{
    const Bytes pixel = pixels(run, x, x + 1);

    pixel[0] = color.r;
    pixel[1] = color.g;
    pixel[2] = color.b;
}

void fillPixels(Bytes run, Rgb color) noexcept
{
    if (run.empty())
    {
        return;
    }
    setPixel(run, 0, color);
    // Doubling copies: a few large memmoves instead of one small write per pixel.
    for (std::size_t filled = kPixelBytes; filled < run.size(); filled *= 2)
    {
        std::ranges::copy(run.first(std::min(filled, run.size() - filled)),
                          run.subspan(filled).begin());
    }
}

// Where the world lands on the canvas in this frame. Below 1 px, a "cell" of the layout is a block
// of 2^shrink × 2^shrink world cells, one pixel, and its coordinates count blocks.
struct Layout
{
    PixelSize          canvas{};
    int                cellSize = 1;
    unsigned           shrink   = 0;
    PixelPoint         offset{};
    core::UniverseRect cells{};  // visible cells (or blocks)
    bool               gridLines  = false;
    int                majorEvery = 0;  // 0 or less: no major lines
    // Canvas columns [left, right) and rows [top, bottom) show the world.
    Pixel left   = 0;
    Pixel right  = 0;
    Pixel top    = 0;
    Pixel bottom = 0;

    [[nodiscard]] Pixel cellLeft(core::UniverseCoord cx) const noexcept
    {
        return (Pixel{cx} * cellSize) - offset.x;
    }
    [[nodiscard]] Pixel cellTop(core::UniverseCoord cy) const noexcept
    {
        return (Pixel{cy} * cellSize) - offset.y;
    }

    // The layout cell that holds world cell c: c itself, or below 1 px its block.
    [[nodiscard]] core::UniverseCoord cellOf(core::UniverseCoord c) const noexcept
    {
        return core::floorDiv(c, std::int64_t{1} << shrink);
    }

    // Whether the grid line in the last pixel column (or row) of cell column (or row) c is major.
    // C++'s remainder keeps the sign, so the lines fall on multiples left of the centre too.
    [[nodiscard]] bool majorLineAfter(core::UniverseCoord c) const noexcept
    {
        return gridLines && majorEvery > 0 && (c + 1) % majorEvery == 0;
    }
};

// The blocks of 2^shrink × 2^shrink cells that hold `cells`; `cells` itself at 1 px and above.
core::UniverseRect blocksOf(core::UniverseRect cells, Scale scale) noexcept
{
    if (cells.empty())
    {
        return {};  // an empty side may still have a length on the other axis
    }
    if (!scale.zoomedOut())
    {
        return cells;
    }
    const std::int64_t side = scale.cellsPerPixel();
    return {.x0 = core::floorDiv(cells.x0, side),
            .y0 = core::floorDiv(cells.y0, side),
            .x1 = core::floorDiv(cells.x1 - 1, side) + 1,
            .y1 = core::floorDiv(cells.y1 - 1, side) + 1};
}

Layout makeLayout(const Viewport& viewport, const RenderStyle& style) noexcept
{
    const Scale scale = viewport.scale();
    // Below 1 px, content pixel p is block p, so the offset needs no change.
    Layout layout{.canvas     = viewport.canvasSize(),
                  .cellSize   = scale.cellSize,
                  .shrink     = scale.shrink,
                  .offset     = viewport.offset(),
                  .cells      = blocksOf(viewport.visibleCells(), scale),
                  .gridLines  = style.gridVisibleAt(scale),
                  .majorEvery = style.majorGridEvery};
    layout.left   = std::max<Pixel>(0, layout.cellLeft(layout.cells.x0));
    layout.right  = std::min(layout.canvas.width, layout.cellLeft(layout.cells.x1));
    layout.top    = std::max<Pixel>(0, layout.cellTop(layout.cells.y0));
    layout.bottom = std::min(layout.canvas.height, layout.cellTop(layout.cells.y1));
    return layout;
}

// One cell's pixel run: `body` colour, with a `line` pixel at the end when grid lines are shown.
void buildStamp(std::vector<std::uint8_t>& stamp, const Layout& layout, Rgb body, Rgb line)
{
    stamp.resize(static_cast<std::size_t>(layout.cellSize) * kPixelBytes);
    fillPixels(stamp, body);
    if (layout.gridLines)
    {
        setPixel(stamp, layout.cellSize - 1, line);
    }
}

// A horizontal grid-line row: `line` across the world, `majorLine` where a major vertical line
// crosses it.
void buildGridRow(std::vector<std::uint8_t>& row, const Layout& layout, Rgb line, Rgb majorLine,
                  Rgb outside)
{
    row.resize(static_cast<std::size_t>(layout.canvas.width) * kPixelBytes);
    fillPixels(pixels(Bytes(row), 0, layout.left), outside);
    fillPixels(pixels(Bytes(row), layout.left, layout.right), line);
    fillPixels(pixels(Bytes(row), layout.right, layout.canvas.width), outside);
    for (core::UniverseCoord cx = layout.cells.x0; cx < layout.cells.x1; ++cx)
    {
        const Pixel x = layout.cellLeft(cx) + layout.cellSize - 1;
        if (layout.majorLineAfter(cx) && x < layout.canvas.width)
        {
            setPixel(row, x, majorLine);
        }
    }
}

// Cell stamps indexed by [major vertical line][alive]. Indexed rather than chosen with branches:
// with random cells at 2 px, a branch per cell doubled the frame time.
using Stamps = std::array<std::array<ConstBytes, 2>, 2>;

// Paints one pixel row through cell row `cellRow`, whose first cell is world column `firstColumn`,
// leaving out its horizontal grid line.
void drawScanline(Bytes scan, std::span<const core::Cell> cellRow, core::UniverseCoord firstColumn,
                  const Layout& layout, const Stamps& stamps, const RenderStyle& style)
{
    fillPixels(pixels(scan, 0, layout.left), style.outside);
    fillPixels(pixels(scan, layout.right, layout.canvas.width), style.outside);
    const auto visible =
        cellRow.subspan(static_cast<std::size_t>(layout.cells.x0 - firstColumn),
                        static_cast<std::size_t>(layout.cells.x1 - layout.cells.x0));

    if (layout.cellSize == 1)
    {
        // 1 px cells: one colour per pixel. (With grid lines, such cells have no body rows.)
        const Bytes out = pixels(scan, layout.left, layout.right);
        assert(out.size() == visible.size() * kPixelBytes);
        auto pixel = out.begin();
        for (const core::Cell cell : visible)
        {
            const Rgb color = cell != core::kDead ? style.alive : style.dead;

            *pixel++ = color.r;
            *pixel++ = color.g;
            *pixel++ = color.b;
        }
        return;
    }

    core::UniverseCoord cx = layout.cells.x0;
    for (const core::Cell cell : visible)
    {
        const auto major = static_cast<std::size_t>(layout.majorLineAfter(cx));
        const auto live  = static_cast<std::size_t>(cell != core::kDead);
        // Both indices are 0 or 1.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const ConstBytes stamp = stamps[major][live];
        // Only the first and last cell can stick out of the canvas.
        const Pixel cellLeft = layout.cellLeft(cx);
        const Pixel from     = std::max<Pixel>(cellLeft, 0);
        const Pixel to       = std::min(cellLeft + layout.cellSize, layout.canvas.width);
        std::ranges::copy(pixels(stamp, from - cellLeft, to - cellLeft),
                          pixels(scan, from, to).begin());
        ++cx;
    }
}

// Makes each block of `blocks` in `window` alive if any of its cells in `grid` is. The block rows
// are split into bands on several threads; `scratch` gets one row per band, in which the cell rows
// of a block row are ORed together before each block's run of it is.
void shrinkGrid(const core::Grid& grid, unsigned shrink, core::UniverseRect blocks,
                core::Grid& window, std::vector<std::vector<core::Cell>>& scratch)
{
    const std::int64_t side  = std::int64_t{1} << shrink;
    const core::Extent world = grid.extent();
    const auto         first = static_cast<std::size_t>(blocks.x0 * side);
    const auto         end =
        static_cast<std::size_t>(std::min<std::int64_t>(blocks.x1 * side, world.width));
    const auto width = end - first;
    const auto rows  = static_cast<core::Coord>(blocks.y1 - blocks.y0);
    const auto read  = static_cast<core::CellCount>(width) * rows * side;

    const unsigned bands = core::suggestedBandCount(read);
    scratch.resize(bands);
    for (std::vector<core::Cell>& row : scratch)
    {
        row.resize(width);
    }
    core::forEachBand(rows, bands, [&](unsigned band, core::Coord firstRow, core::Coord endRow) {
        const std::span<core::Cell> any = scratch.at(band);
        for (core::Coord row = firstRow; row < endRow; ++row)
        {
            const std::int64_t top    = (blocks.y0 + row) * side;
            const std::int64_t bottom = std::min<std::int64_t>(top + side, world.height);
            std::ranges::copy(grid.row(static_cast<core::Coord>(top)).subspan(first, width),
                              any.begin());
            for (std::int64_t y = top + 1; y < bottom; ++y)
            {
                std::ranges::transform(any,
                                       grid.row(static_cast<core::Coord>(y)).subspan(first, width),
                                       any.begin(), std::bit_or<>{});
            }
            // Cells are 0 or 1, so ORing a block's run tells whether any is alive.
            const std::span<core::Cell> out = window.row(row);
            for (std::size_t block = 0; block < out.size(); ++block)
            {
                const std::size_t from  = block * static_cast<std::size_t>(side);
                const std::size_t to    = std::min(from + static_cast<std::size_t>(side), width);
                core::Cell        alive = core::kDead;
                for (const core::Cell cell : any.subspan(from, to - from))
                {
                    alive |= cell;
                }
                out[block] = alive;
            }
        }
    });
}

}  // namespace

void Rasterizer::render(const core::Grid& grid, const Viewport& viewport, const RenderStyle& style,
                        PixelBuffer& out)
{
    assert(!viewport.unbounded() && viewport.worldExtent() == grid.extent());
    const Scale scale = viewport.scale();
    if (!scale.zoomedOut())
    {
        paint(grid, {}, viewport, style, out);
        return;
    }
    // Below 1 px, a grid of blocks, one per pixel, is painted instead of the cells.
    const core::UniverseRect blocks = blocksOf(viewport.visibleCells(), scale);
    resetWindow(blocks);
    shrinkGrid(grid, scale.shrink, blocks, m_window, m_bandRows);
    paint(m_window, {.x = blocks.x0, .y = blocks.y0}, viewport, style, out);
}

void Rasterizer::render(const core::HashLife& plane, const Viewport& viewport,
                        const RenderStyle& style, PixelBuffer& out)
{
    assert(viewport.unbounded());
    const Scale              scale   = viewport.scale();
    const core::UniverseRect visible = viewport.visibleCells();
    const core::UniverseRect blocks  = blocksOf(visible, scale);
    resetWindow(blocks);
    // forEachBlock() aligns its blocks as the pixels are: to multiples of 2^shrink cells.
    plane.forEachBlock(visible, scale.shrink, [&](core::UniversePos corner) {
        m_window.set({.x = static_cast<core::Coord>(
                          core::floorDiv(corner.x, scale.cellsPerPixel()) - blocks.x0),
                      .y = static_cast<core::Coord>(
                          core::floorDiv(corner.y, scale.cellsPerPixel()) - blocks.y0)},
                     core::kAlive);
    });
    paint(m_window, {.x = blocks.x0, .y = blocks.y0}, viewport, style, out);
}

void Rasterizer::resetWindow(core::UniverseRect cells)
{
    const core::Extent extent{
        .width  = static_cast<core::Coord>(std::max<core::UniverseCoord>(cells.x1 - cells.x0, 0)),
        .height = static_cast<core::Coord>(std::max<core::UniverseCoord>(cells.y1 - cells.y0, 0))};
    if (m_window.extent() == extent)
    {
        m_window.clear();
    }
    else
    {
        m_window = core::Grid(extent);
    }
}

void Rasterizer::paint(const core::Grid& grid, core::UniversePos origin, const Viewport& viewport,
                       const RenderStyle& style, PixelBuffer& out)
{
    out.resize(viewport.canvasSize());
    const Layout layout = makeLayout(viewport, style);
    if (layout.cells.empty())  // also an empty canvas
    {
        out.fill(style.outside);
        return;
    }

    // Rows above and below the world.
    const Pixel width = layout.canvas.width;
    fillPixels(pixels(out.bytes(), 0, layout.top * width), style.outside);
    fillPixels(pixels(out.bytes(), layout.bottom * width, layout.canvas.height * width),
               style.outside);

    // Everything a cell row is copied from, built once per frame.
    buildStamp(m_deadStamp, layout, style.dead, style.gridLine);
    buildStamp(m_aliveStamp, layout, style.alive, style.gridLine);
    buildStamp(m_deadStampMajor, layout, style.dead, style.gridLineMajor);
    buildStamp(m_aliveStampMajor, layout, style.alive, style.gridLineMajor);
    const Stamps stamps{{{m_deadStamp, m_aliveStamp}, {m_deadStampMajor, m_aliveStampMajor}}};
    if (layout.gridLines)
    {
        buildGridRow(m_gridRow, layout, style.gridLine, style.gridLineMajor, style.outside);
        buildGridRow(m_gridRowMajor, layout, style.gridLineMajor, style.gridLineMajor,
                     style.outside);
    }

    for (core::UniverseCoord cy = layout.cells.y0; cy < layout.cells.y1; ++cy)
    {
        // Cell row cy covers pixel rows [cellTop, lineRow]; with grid lines, lineRow is the line.
        const Pixel cellTop = layout.cellTop(cy);
        const Pixel lineRow = cellTop + layout.cellSize - 1;
        const Pixel first   = std::max<Pixel>(cellTop, 0);
        const Pixel bodyEnd =
            std::min(layout.gridLines ? lineRow : lineRow + 1, layout.canvas.height);

        // Paint the first visible body row, then copy it. Skipped when only the line row is
        // visible.
        if (first < bodyEnd)
        {
            const Bytes scan = out.row(first);
            drawScanline(scan, grid.row(static_cast<core::Coord>(cy - origin.y)), origin.x, layout,
                         stamps, style);
            for (Pixel y = first + 1; y < bodyEnd; ++y)
            {
                std::ranges::copy(scan, out.row(y).begin());
            }
        }
        if (layout.gridLines && lineRow < layout.canvas.height)
        {
            std::ranges::copy(layout.majorLineAfter(cy) ? m_gridRowMajor : m_gridRow,
                              out.row(lineRow).begin());
        }
    }
}

void drawAnts(std::span<const core::Ant> ants, const Viewport& viewport, const RenderStyle& style,
              PixelBuffer& out)
{
    assert(out.size() == viewport.canvasSize());
    const Layout layout = makeLayout(viewport, style);
    if (layout.cells.empty())
    {
        return;
    }
    // The cell without its grid line, so an ant never paints over one.
    const Pixel body = layout.gridLines ? layout.cellSize - 1 : layout.cellSize;

    for (const core::Ant& ant : ants)
    {
        const core::UniversePos cell{.x = layout.cellOf(ant.position.x),
                                     .y = layout.cellOf(ant.position.y)};
        if (cell.x < layout.cells.x0 || cell.x >= layout.cells.x1 || cell.y < layout.cells.y0 ||
            cell.y >= layout.cells.y1)
        {
            continue;
        }
        // Only an ant at the edge of the view can stick out of the canvas.
        const Pixel left   = std::max<Pixel>(layout.cellLeft(cell.x), 0);
        const Pixel right  = std::min(layout.cellLeft(cell.x) + body, layout.canvas.width);
        const Pixel top    = std::max<Pixel>(layout.cellTop(cell.y), 0);
        const Pixel bottom = std::min(layout.cellTop(cell.y) + body, layout.canvas.height);
        if (left >= right || top >= bottom)  // nothing of its body is on the canvas
        {
            continue;
        }
        for (Pixel y = top; y < bottom; ++y)
        {
            fillPixels(pixels(out.row(y), left, right), style.ant);
        }
    }
}

}  // namespace wxLife::render
