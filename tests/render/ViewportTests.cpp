#include "wxLife/render/Viewport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Random.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{
namespace
{

using core::Coord;
using core::Extent;

// Positions and rectangles are compared as text, so a failure prints readable values.
std::string text(PixelPoint p)
{
    return std::format("({}, {})", p.x, p.y);
}

std::string text(core::UniversePos c)
{
    return std::format("({}, {})", c.x, c.y);
}

std::string text(std::optional<core::UniversePos> c)
{
    return c ? text(*c) : "outside";
}

std::string text(core::UniverseRect r)
{
    return r.empty() ? "empty" : std::format("[{}, {}) x [{}, {})", r.x0, r.x1, r.y0, r.y1);
}

Viewport makeViewport(Extent world, PixelSize canvas, Scale scale, PixelPoint offset = {})
{
    Viewport viewport;
    viewport.setWorldExtent(world);
    viewport.setCanvasSize(canvas);
    viewport.setScale(scale, {});
    viewport.scrollTo(offset);
    return viewport;
}

Viewport makeViewport(Extent world, PixelSize canvas, int cellSize, PixelPoint offset = {})
{
    return makeViewport(world, canvas, Scale{.cellSize = cellSize}, offset);
}

// Pixels per cell side as a number: 4, or 1/16 below 1 px.
double pixelsPerCell(Scale scale)
{
    return scale.zoomedOut() ? 1.0 / static_cast<double>(scale.cellsPerPixel())
                             : static_cast<double>(scale.cellSize);
}

// Uniform in [lo, hi]; the modulo bias does not matter here.
std::int64_t pick(core::SplitMix64& rng, std::int64_t lo, std::int64_t hi)
{
    return lo + static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(hi - lo + 1));
}

// Small worlds and canvases, any cell size, any scroll position (including centred axes).
Viewport randomViewport(core::SplitMix64& rng)
{
    const Extent     world{.width  = static_cast<Coord>(pick(rng, 0, 60)),
                           .height = static_cast<Coord>(pick(rng, 0, 60))};
    const PixelSize  canvas{.width = pick(rng, 0, 400), .height = pick(rng, 0, 300)};
    const int        cellSize = static_cast<int>(pick(rng, kMinCellSize, kMaxCellSize));
    const PixelPoint offset{.x = pick(rng, -50, (Pixel{world.width} * cellSize) + 50),
                            .y = pick(rng, -50, (Pixel{world.height} * cellSize) + 50)};
    return makeViewport(world, canvas, cellSize, offset);
}

// A world position in cells; (0.5, 0.5) is the centre of cell (0, 0).
struct WorldPoint
{
    double x = 0;
    double y = 0;
};

// The world position of the centre of canvas pixel `p`. Exact for the magnitudes used here.
WorldPoint worldAt(const Viewport& viewport, PixelPoint p)
{
    const auto axis = [&](Pixel offset, Pixel canvas) {
        return (static_cast<double>(offset + canvas) + 0.5) / pixelsPerCell(viewport.scale());
    };
    return {.x = axis(viewport.offset().x, p.x), .y = axis(viewport.offset().y, p.y)};
}

// `point` lies inside canvas pixel `p` (edges included): at most half a pixel from its centre.
void expectInsidePixel(const Viewport& viewport, PixelPoint p, WorldPoint point)
{
    // 1e-9 absorbs rounding.
    const double     halfPixel = (0.5 / pixelsPerCell(viewport.scale())) + 1e-9;
    const WorldPoint centre    = worldAt(viewport, p);
    EXPECT_NEAR(centre.x, point.x, halfPixel);
    EXPECT_NEAR(centre.y, point.y, halfPixel);
}

// The first and one-past-last cell whose pixels overlap [0, canvas), found by trying every cell.
struct Span
{
    Coord first = 0;
    Coord end   = 0;
};

Span bruteForceVisible(Pixel offset, Pixel canvas, Coord side, int cellSize)
{
    std::optional<Coord> first;
    Coord                end = 0;
    for (Coord c = 0; c < side; ++c)
    {
        // Pixels [left, left + cellSize) overlap [0, canvas).
        const Pixel left = (Pixel{c} * cellSize) - offset;
        if (std::max<Pixel>(left, 0) < std::min(left + cellSize, canvas))
        {
            first = first.value_or(c);
            end   = c + 1;
        }
    }
    return first ? Span{.first = *first, .end = end} : Span{};
}

// Below 1 px: the cells whose pixel, floor(c / perPixel) - offset, lies in [0, canvas), found by
// trying every cell.
Span bruteForceVisibleBlocks(Pixel offset, Pixel canvas, Coord side, std::int64_t perPixel)
{
    std::optional<Coord> first;
    Coord                end = 0;
    for (Coord c = 0; c < side; ++c)
    {
        const Pixel pixel = core::floorDiv(c, perPixel) - offset;
        if (pixel >= 0 && pixel < canvas)
        {
            first = first.value_or(c);
            end   = c + 1;
        }
    }
    return Span{.first = first.value_or(0), .end = end};
}

// Below 1 px: the first cell that content pixel `content` shows, found by trying every cell.
std::optional<Coord> bruteForceFirstCell(Pixel content, Coord side, std::int64_t perPixel)
{
    for (Coord c = 0; c < side; ++c)
    {
        if (core::floorDiv(c, perPixel) == content)
        {
            return c;
        }
    }
    return std::nullopt;
}

// The cell whose pixel square contains the canvas point, found by trying every cell.
std::optional<core::UniversePos> bruteForceCellAt(const Viewport& viewport, PixelPoint p)
{
    const int  size = viewport.scale().cellSize;
    const auto axis = [size](Pixel offset, Pixel point, Coord side) -> std::optional<Coord> {
        for (Coord c = 0; c < side; ++c)
        {
            const Pixel left = (Pixel{c} * size) - offset;
            if (left <= point && point < left + size)
            {
                return c;
            }
        }
        return std::nullopt;
    };
    const auto x = axis(viewport.offset().x, p.x, viewport.worldExtent().width);
    const auto y = axis(viewport.offset().y, p.y, viewport.worldExtent().height);
    return x && y ? std::optional(core::UniversePos{.x = *x, .y = *y}) : std::nullopt;
}

TEST(ViewportTest, StartsEmpty)
{
    const Viewport viewport;
    EXPECT_EQ(viewport.worldExtent(), (Extent{}));
    EXPECT_EQ(viewport.canvasSize(), (PixelSize{}));
    EXPECT_EQ(viewport.scale().cellSize, 4);
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");
    EXPECT_EQ(text(viewport.visibleCells()), "empty");
}

TEST(ViewportTest, NearestZoomStep)
{
    static_assert(kZoomSteps[nearestZoomStep(1)] == 1);
    static_assert(kZoomSteps[nearestZoomStep(7)] == 6);  // ties go to the smaller entry
    static_assert(kZoomSteps[nearestZoomStep(9)] == 8);
    static_assert(kZoomSteps[nearestZoomStep(14)] == 12);
    static_assert(kZoomSteps[nearestZoomStep(15)] == 16);
    static_assert(kZoomSteps[nearestZoomStep(90)] == 80);
    static_assert(kZoomSteps[nearestZoomStep(91)] == 100);
    static_assert(nearestZoomStep(0) == 0);
    static_assert(nearestZoomStep(std::numeric_limits<int>::min()) == 0);
    static_assert(nearestZoomStep(std::numeric_limits<int>::max()) == kZoomSteps.size() - 1);
    for (std::size_t i = 0; const int step : kZoomSteps)
    {
        EXPECT_EQ(nearestZoomStep(step), i++);
    }
}

TEST(ViewportTest, CellSizeIsClamped)
{
    Viewport viewport = makeViewport({.width = 10, .height = 10}, {.width = 100, .height = 100}, 4);
    viewport.setCellSize(0, {});
    EXPECT_EQ(viewport.scale().cellSize, kMinCellSize);
    viewport.setCellSize(101, {});
    EXPECT_EQ(viewport.scale().cellSize, kMaxCellSize);
    viewport.setCellSize(std::numeric_limits<int>::min(), {});
    EXPECT_EQ(viewport.scale().cellSize, kMinCellSize);
    viewport.setCellSize(37, {});  // any size in range is kept exactly
    EXPECT_EQ(viewport.scale().cellSize, 37);
}

TEST(ViewportTest, AnchoredZoomKeepsThePointUnderTheAnchor)
{
    const PixelSize canvas{.width = 640, .height = 480};
    for (const int from : kZoomSteps)
    {
        // The canvas corners, and an anchor at each position inside a cell along x (y moves 3 px
        // each time).
        std::vector anchors{PixelPoint{.x = 0, .y = 0}, PixelPoint{.x = 639, .y = 479}};
        for (Pixel d = 0; d < from; ++d)
        {
            anchors.push_back({.x = 300 + d, .y = 100 + (3 * d)});
        }
        for (const int to : kZoomSteps)
        {
            for (const PixelPoint anchor : anchors)
            {
                Viewport viewport =
                    makeViewport({.width = 100'000, .height = 100'000}, canvas, from);
                viewport.centerOn({.x = 50'000, .y = 49'000});
                const std::optional<core::UniversePos> cell  = viewport.cellAt(anchor);
                const WorldPoint                       point = worldAt(viewport, anchor);
                ASSERT_TRUE(cell);

                viewport.setCellSize(to, anchor);
                SCOPED_TRACE(std::format("{} px -> {} px at {}", from, to, text(anchor)));
                // The test is only meaningful when the new offset was not clamped.
                const PixelSize content = viewport.contentSize();
                ASSERT_GT(viewport.offset().x, 0);
                ASSERT_GT(viewport.offset().y, 0);
                ASSERT_LT(viewport.offset().x, content.width - canvas.width);
                ASSERT_LT(viewport.offset().y, content.height - canvas.height);

                EXPECT_EQ(viewport.scale().cellSize, to);
                EXPECT_EQ(text(viewport.cellAt(anchor)), text(cell));
                expectInsidePixel(viewport, anchor, point);
            }
        }
    }
}

TEST(ViewportTest, AnchoredZoomAlsoHoldsLeftOfTheWorld)
{
    // Any point can be the anchor. Here it is content pixel -401 (cell -101, 3 px in), so the
    // arithmetic must round toward minus infinity.
    Viewport         viewport = makeViewport({.width = 10'000, .height = 10'000},
                                             {.width = 640, .height = 480}, 4, {.x = 99, .y = 0});
    const PixelPoint anchor{.x = -500, .y = 0};
    const auto       cellUnderAnchor = [&] {
        return core::floorDiv(viewport.offset().x + anchor.x, viewport.scale().cellSize);
    };
    ASSERT_EQ(cellUnderAnchor(), -101);
    viewport.setCellSize(1, anchor);
    EXPECT_EQ(viewport.offset().x, 399);
    EXPECT_EQ(cellUnderAnchor(), -101);
}

TEST(ViewportTest, RepeatedZoomingDoesNotDrift)
{
    // Every zoom of a run keeps the run's first point, and each scale always gets the same offset,
    // below 1 px too. Zoomed out until an axis is centred, the run ends and a new one starts.
    core::SplitMix64 rng(7);
    const PixelSize  canvas{.width = 400, .height = 300};
    Viewport         viewport = makeViewport({.width = 100'000, .height = 100'000}, canvas, 8);
    viewport.centerOn({.x = 31'415, .y = 27'182});
    const PixelPoint            anchor{.x = 313, .y = 207};
    WorldPoint                  point = worldAt(viewport, anchor);
    std::map<Scale, PixelPoint> offsets{{viewport.scale(), viewport.offset()}};
    int                         restarts = 0;
    for (int i = 0; i < 2000; ++i)
    {
        if (i % 2 == 0)
        {
            viewport.zoomBy(static_cast<int>(pick(rng, -3, 3)), anchor);
        }
        else
        {
            viewport.setCellSize(static_cast<int>(pick(rng, kMinCellSize, kMaxCellSize)), anchor);
        }
        SCOPED_TRACE(std::format("after {} zooms, at {}", i + 1, toString(viewport.scale())));
        const PixelSize content = viewport.contentSize();
        if (content.width <= canvas.width || content.height <= canvas.height)
        {
            point = worldAt(viewport, anchor);
            offsets.clear();
            ++restarts;
            continue;
        }
        expectInsidePixel(viewport, anchor, point);
        // The first visit of a scale records its offset; later visits must match it.
        const PixelPoint expected =
            offsets.try_emplace(viewport.scale(), viewport.offset()).first->second;
        ASSERT_EQ(text(viewport.offset()), text(expected));
    }
    EXPECT_GT(offsets.size(), 20U);  // most scales, the ones below 1 px included
    EXPECT_LT(restarts, 200);
}

TEST(ViewportTest, ZoomingThereAndBackRestoresTheOffset)
{
    // From every size and every anchor position inside a cell, zoom one step at a time to either
    // end of kZoomSteps and back, or jiggle between neighbours. The world is large, so nothing is
    // clamped.
    const int last = static_cast<int>(kZoomSteps.size()) - 1;
    for (int start = 0; start <= last; ++start)
    {
        const int size = kZoomSteps.at(static_cast<std::size_t>(start));
        for (Pixel d = 0; d < size; ++d)
        {
            const PixelPoint anchor{.x = 400 + d, .y = 300 + d};
            // Each walk moves |walk| steps (in if positive), one step at a time.
            const auto offsetAfter = [&](const std::vector<int>& walks) {
                Viewport viewport = makeViewport({.width = 100'000, .height = 100'000},
                                                 {.width = 800, .height = 600}, size);
                viewport.centerOn({.x = 50'000, .y = 50'000});
                for (const int walk : walks)
                {
                    for (int i = 0; i < std::abs(walk); ++i)
                    {
                        viewport.zoomBy(walk > 0 ? 1 : -1, anchor);
                    }
                }
                EXPECT_EQ(viewport.scale().cellSize, size);
                return text(viewport.offset());
            };
            SCOPED_TRACE(std::format("from {} px at {}", size, text(anchor)));
            const std::string home = offsetAfter({});
            const int         up   = last - start;
            const int         down = -start;
            EXPECT_EQ(offsetAfter({up, -up}), home) << "in and back out";
            EXPECT_EQ(offsetAfter({down, -down}), home) << "out and back in";

            const int neighbour =
                start > 0 ? -1 : 1;  // one step out and back in (at 1 px: in and out)
            std::vector<int> jiggle;
            for (int i = 0; i < 20; ++i)
            {
                jiggle.insert(jiggle.end(), {neighbour, -neighbour});
            }
            EXPECT_EQ(offsetAfter(jiggle), home) << "jiggle";
        }
    }
}

TEST(ViewportTest, ZoomingInFromOnePixelKeepsTheAnchorAtTheCellCentre)
{
    // At 1 px the anchor pixel is a whole cell, so the kept point is the cell's centre. Step by
    // step up to 100 px, the anchor must end next to that centre (pixel 50 of 100), not in a corner
    // of the cell.
    Viewport viewport =
        makeViewport({.width = 10'000, .height = 10'000}, {.width = 800, .height = 600}, 1);
    viewport.centerOn({.x = 5000, .y = 5000});
    const PixelPoint                       anchor{.x = 537, .y = 300};
    const std::optional<core::UniversePos> cell = viewport.cellAt(anchor);
    ASSERT_TRUE(cell);
    while (viewport.scale().cellSize < kMaxCellSize)
    {
        viewport.zoomBy(1, anchor);
    }
    EXPECT_EQ(text(viewport.cellAt(anchor)), text(cell));
    EXPECT_EQ(text(viewport.cellOrigin(cell.value())),
              text(PixelPoint{anchor.x - 50, anchor.y - 50}));
}

TEST(ViewportTest, ACentredAxisDoesNotEndTheOtherAxisRun)
{
    // A tall world in a wide canvas: x stays centred for the first zooms, and centring counts as
    // clamping. y is never clamped, so its run from 1 px must still end at the cell centre, as in
    // the test above.
    Viewport viewport =
        makeViewport({.width = 100, .height = 1000}, {.width = 2000, .height = 1000}, 1);
    const PixelPoint                       anchor{.x = 1000, .y = 500};
    const std::optional<core::UniversePos> cell = viewport.cellAt(anchor);
    ASSERT_EQ(text(cell), "(50, 500)");
    while (viewport.scale().cellSize < kMaxCellSize)
    {
        viewport.zoomBy(1, anchor);
    }
    const std::optional<core::UniversePos> now = viewport.cellAt(anchor);
    ASSERT_TRUE(now);
    EXPECT_EQ(now.value().y, cell.value().y);
    EXPECT_EQ(anchor.y - viewport.cellOrigin(now.value()).y, 50);
}

TEST(ViewportTest, OtherCameraChangesEndARunOfZooms)
{
    // After any other change, the next zoom keeps the cell that is under the anchor now, not the
    // point the run started on.
    const PixelPoint anchor{.x = 400, .y = 300};
    const auto       check = [&](std::string_view name, const auto& move) {
        Viewport viewport =
            makeViewport({.width = 20'000, .height = 20'000}, {.width = 800, .height = 600}, 10);
        viewport.centerOn({.x = 5000, .y = 5000});
        viewport.zoomBy(1, anchor);
        move(viewport);
        const std::optional<core::UniversePos> cell = viewport.cellAt(anchor);
        viewport.zoomBy(1, anchor);
        EXPECT_EQ(text(viewport.cellAt(anchor)), text(cell)) << name;
    };
    check("panBy", [](Viewport& v) { v.panBy(37, -41); });
    check("scrollTo",
          [](Viewport& v) { v.scrollTo({.x = v.offset().x - 50, .y = v.offset().y + 70}); });
    check("centerOn", [](Viewport& v) { v.centerOn({.x = 4990, .y = 5010}); });
    // (fitWorld() would zoom out until the world is centred, and centring moves the next zoom.)
    check("fitCells",
          [](Viewport& v) { v.fitCells({.x0 = 4000, .y0 = 4000, .x1 = 4100, .y1 = 4100}); });
    check("setWorldExtent", [](Viewport& v) {
        v.setWorldExtent({.width = 5010, .height = 5010});
    });  // clamps the offset

    // A zoom that clamping moved ends the run too. At 80 px this one wants offset -90, which
    // becomes 0.
    Viewport edge =
        makeViewport({.width = 10'000, .height = 10'000}, {.width = 800, .height = 600}, 100);
    const PixelPoint nearEdge{.x = 450, .y = 300};
    edge.zoomBy(-1, nearEdge);
    ASSERT_EQ(text(edge.offset()), "(0, 0)");
    const std::optional<core::UniversePos> cell = edge.cellAt(nearEdge);
    edge.zoomBy(1, nearEdge);
    EXPECT_EQ(text(edge.cellAt(nearEdge)), text(cell));
}

TEST(ViewportTest, SmallWorldIsCentred)
{
    // Content 40 × 20 px on a 100 × 50 canvas: 30 px left over on each side, 15 above and below.
    Viewport viewport = makeViewport({.width = 10, .height = 5}, {.width = 100, .height = 50}, 4);
    EXPECT_EQ(viewport.contentSize(), (PixelSize{40, 20}));
    EXPECT_EQ(text(viewport.offset()), "(-30, -15)");

    // Scrolling cannot move a centred axis.
    viewport.scrollTo({.x = 500, .y = -500});
    EXPECT_EQ(text(viewport.offset()), "(-30, -15)");
    viewport.panBy(7, 7);
    EXPECT_EQ(text(viewport.offset()), "(-30, -15)");

    // An odd leftover puts the extra pixel on the right and at the bottom.
    viewport.setCanvasSize({.width = 101, .height = 51});
    EXPECT_EQ(text(viewport.offset()), "(-30, -15)");

    // Exactly filling the canvas gives offset 0.
    viewport.setCanvasSize({.width = 40, .height = 20});
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");
}

TEST(ViewportTest, LargeWorldOffsetStaysInRange)
{
    // Content 4000 × 4000 px on a 640 × 480 canvas: offsets range over [0, 3360] × [0, 3520].
    Viewport viewport =
        makeViewport({.width = 1000, .height = 1000}, {.width = 640, .height = 480}, 4);
    viewport.scrollTo({.x = -50, .y = -1});
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");
    viewport.scrollTo({.x = 1'000'000'000, .y = 3520});
    EXPECT_EQ(text(viewport.offset()), "(3360, 3520)");
    viewport.scrollTo({.x = 100, .y = 200});
    EXPECT_EQ(text(viewport.offset()), "(100, 200)");
    viewport.panBy(10, -5);
    EXPECT_EQ(text(viewport.offset()), "(110, 195)");
    viewport.panBy(-1000, 1'000'000);
    EXPECT_EQ(text(viewport.offset()), "(0, 3520)");
}

TEST(ViewportTest, EachAxisIsClampedOnItsOwn)
{
    // Wide and flat: scrolls horizontally, centred vertically.
    Viewport const viewport = makeViewport({.width = 1000, .height = 5},
                                           {.width = 640, .height = 480}, 4, {.x = 123, .y = 456});
    EXPECT_EQ(text(viewport.offset()), "(123, -230)");
}

TEST(ViewportTest, ResizingReclampsTheOffset)
{
    Viewport viewport = makeViewport({.width = 1000, .height = 1000}, {.width = 640, .height = 480},
                                     4, {.x = 3360, .y = 3520});

    viewport.setWorldExtent(
        {.width = 500, .height = 1000});  // content 2000 wide: offset.x may be at most 1360
    EXPECT_EQ(text(viewport.offset()), "(1360, 3520)");

    viewport.setCanvasSize(
        {.width = 2100, .height = 480});  // now the world is narrower than the canvas
    EXPECT_EQ(text(viewport.offset()), "(-50, 3520)");

    viewport.setCanvasSize(
        {.width = 640, .height = 480});  // the centred offset is clamped back into range
    EXPECT_EQ(text(viewport.offset()), "(0, 3520)");
}

TEST(ViewportTest, CellAtUsesFloorDivision)
{
    // The world starts at canvas pixel (30, 15), so content coordinates left of it are negative.
    const Viewport viewport =
        makeViewport({.width = 10, .height = 5}, {.width = 100, .height = 50}, 4);
    EXPECT_EQ(text(viewport.cellAt({30, 15})), "(0, 0)");
    EXPECT_EQ(text(viewport.cellAt({33, 18})), "(0, 0)");
    EXPECT_EQ(text(viewport.cellAt({34, 15})), "(1, 0)");
    EXPECT_EQ(text(viewport.cellAt({69, 34})), "(9, 4)");

    // Truncating division would put these just-outside pixels into cell 0.
    EXPECT_EQ(text(viewport.cellAt({29, 15})), "outside");
    EXPECT_EQ(text(viewport.cellAt({30, 14})), "outside");
    EXPECT_EQ(text(viewport.cellAt({27, 12})), "outside");

    EXPECT_EQ(text(viewport.cellAt({70, 34})), "outside");
    EXPECT_EQ(text(viewport.cellAt({69, 35})), "outside");
    EXPECT_EQ(text(viewport.cellAt({-1000, -1000})), "outside");
}

TEST(ViewportTest, CellAtClampedStaysInTheWorld)
{
    const Viewport viewport =
        makeViewport({.width = 10, .height = 5}, {.width = 100, .height = 50}, 4);
    EXPECT_EQ(text(viewport.cellAtClamped({29, 14})), "(0, 0)");
    EXPECT_EQ(text(viewport.cellAtClamped({-1000, 20})), "(0, 1)");
    EXPECT_EQ(text(viewport.cellAtClamped({50, -3})), "(5, 0)");
    EXPECT_EQ(text(viewport.cellAtClamped({1000, 1000})), "(9, 4)");
    EXPECT_EQ(text(viewport.cellAtClamped({45, 25})), text(viewport.cellAt({45, 25})));
}

TEST(ViewportTest, CellOriginIsTheCanvasPixelOfTheTopLeftCorner)
{
    const Viewport centred =
        makeViewport({.width = 10, .height = 5}, {.width = 100, .height = 50}, 4);
    EXPECT_EQ(text(centred.cellOrigin({0, 0})), "(30, 15)");
    EXPECT_EQ(text(centred.cellOrigin({9, 4})), "(66, 31)");
    EXPECT_EQ(text(centred.cellOrigin({-1, -1})), "(26, 11)");  // also defined outside the world

    const Viewport scrolled = makeViewport({.width = 1000, .height = 1000},
                                           {.width = 640, .height = 480}, 4, {.x = 101, .y = 202});
    EXPECT_EQ(text(scrolled.cellOrigin({0, 0})), "(-101, -202)");
    EXPECT_EQ(text(scrolled.cellOrigin({25, 50})), "(-1, -2)");
    EXPECT_EQ(text(scrolled.cellOrigin({26, 51})), "(3, 2)");
}

TEST(ViewportTest, CellAtMatchesBruteForce)
{
    core::SplitMix64 rng(11);
    for (int i = 0; i < 300; ++i)
    {
        const Viewport viewport = randomViewport(rng);
        for (int j = 0; j < 20; ++j)
        {
            const PixelPoint p{.x = pick(rng, -120, 520), .y = pick(rng, -120, 420)};
            ASSERT_EQ(text(viewport.cellAt(p)), text(bruteForceCellAt(viewport, p)))
                << "point " << text(p) << ", offset " << text(viewport.offset()) << ", cell size "
                << viewport.scale().cellSize;
        }
    }
}

TEST(ViewportTest, CellOriginAndCellAtAreInverses)
{
    core::SplitMix64 rng(13);
    for (int i = 0; i < 300; ++i)
    {
        const Viewport viewport = randomViewport(rng);
        const Extent   world    = viewport.worldExtent();
        if (world.cellCount() == 0)
        {
            continue;
        }
        const int               size = viewport.scale().cellSize;
        const core::UniversePos cell{.x = pick(rng, 0, world.width - 1),
                                     .y = pick(rng, 0, world.height - 1)};
        const PixelPoint        origin = viewport.cellOrigin(cell);
        const PixelPoint        last{.x = origin.x + size - 1, .y = origin.y + size - 1};
        EXPECT_EQ(text(viewport.cellAt(origin)), text(cell));
        EXPECT_EQ(text(viewport.cellAt(last)), text(cell));
        EXPECT_EQ(text(viewport.cellAtClamped(last)), text(cell));
        EXPECT_NE(text(viewport.cellAt({origin.x - 1, origin.y})), text(cell));
        EXPECT_NE(text(viewport.cellAt({origin.x, last.y + 1})), text(cell));
    }
}

TEST(ViewportTest, VisibleCellsMatchBruteForce)
{
    core::SplitMix64 rng(17);
    for (int i = 0; i < 2000; ++i)
    {
        const Viewport  viewport = randomViewport(rng);
        const PixelSize canvas   = viewport.canvasSize();
        const Extent    world    = viewport.worldExtent();
        const Span      xs       = bruteForceVisible(viewport.offset().x, canvas.width, world.width,
                                                     viewport.scale().cellSize);
        const Span      ys = bruteForceVisible(viewport.offset().y, canvas.height, world.height,
                                               viewport.scale().cellSize);
        const core::UniverseRect expected{
            .x0 = xs.first, .y0 = ys.first, .x1 = xs.end, .y1 = ys.end};
        ASSERT_EQ(text(viewport.visibleCells()), text(expected))
            << "world " << world.width << "x" << world.height << ", canvas " << canvas.width << "x"
            << canvas.height << ", offset " << text(viewport.offset()) << ", cell size "
            << viewport.scale().cellSize;
    }
}

TEST(ViewportTest, VisibleCellsIncludePartlyVisibleCells)
{
    // Offset 6 at 4 px: canvas x 0..1 shows half of cell 1, and x 98..99 half of cell 26.
    const Viewport viewport = makeViewport({.width = 1000, .height = 1000},
                                           {.width = 100, .height = 50}, 4, {.x = 6, .y = 0});
    EXPECT_EQ(text(viewport.visibleCells()), "[1, 27) x [0, 13)");
}

TEST(ViewportTest, FitWorldPicksTheLargestSizeThatShowsEverything)
{
    Viewport viewport = makeViewport({.width = 100, .height = 50}, {.width = 1000, .height = 800},
                                     4, {.x = 1, .y = 1});
    viewport.fitWorld();
    EXPECT_EQ(viewport.scale().cellSize, 10);  // min(1000 / 100, 800 / 50)
    EXPECT_EQ(text(viewport.offset()), "(0, -150)");
    EXPECT_EQ(text(viewport.visibleCells()), "[0, 100) x [0, 50)");

    viewport.setWorldExtent({.width = 512, .height = 512});
    viewport.setCanvasSize({.width = 1200, .height = 800});
    viewport.fitWorld();
    EXPECT_EQ(viewport.scale().cellSize, 1);
    EXPECT_EQ(text(viewport.offset()), "(-344, -144)");
}

TEST(ViewportTest, FitWorldStopsAtTheLargestCellSize)
{
    Viewport viewport = makeViewport({.width = 5, .height = 5}, {.width = 1000, .height = 1000}, 4);
    viewport.fitWorld();
    EXPECT_EQ(viewport.scale().cellSize, kMaxCellSize);
    EXPECT_EQ(text(viewport.offset()), "(-250, -250)");
}

TEST(ViewportTest, FitWorldZoomsOutBelowOnePixel)
{
    // At 1 px the world is larger than the canvas; at 1/32 px it is 625 × 625 pixels, centred.
    Viewport viewport =
        makeViewport({.width = 20'000, .height = 20'000}, {.width = 1000, .height = 800}, 16);
    viewport.fitWorld();
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 5}));
    EXPECT_EQ(text(viewport.offset()), "(-187, -87)");
    EXPECT_EQ(text(viewport.cellAt({500, 400})), "(10016, 10016)");
    EXPECT_EQ(text(viewport.visibleCells()), "[0, 20000) x [0, 20000)");
}

TEST(ViewportTest, FitWorldHandlesEmptyInputs)
{
    Viewport noCanvas = makeViewport({.width = 10, .height = 10}, {.width = 0, .height = 0}, 50);
    noCanvas.fitWorld();
    EXPECT_EQ(noCanvas.scale().cellSize, kMinCellSize);
    EXPECT_EQ(text(noCanvas.visibleCells()), "empty");

    Viewport noWorld;
    noWorld.setCanvasSize({.width = 300, .height = 200});
    noWorld.fitWorld();
    EXPECT_EQ(noWorld.scale().cellSize, kMaxCellSize);
    EXPECT_EQ(text(noWorld.offset()), "(-150, -100)");
    EXPECT_EQ(text(noWorld.visibleCells()), "empty");
}

TEST(ViewportTest, FitCellsShowsPartOfTheWorld)
{
    // 40 × 20 cells on an 800 × 600 canvas: 20 px per cell (min(800 / 40, 600 / 20)), with the
    // middle of the cells, (120, 210), in the middle of the canvas.
    Viewport viewport =
        makeViewport({.width = 1000, .height = 1000}, {.width = 800, .height = 600}, 4);
    viewport.fitCells({.x0 = 100, .y0 = 200, .x1 = 140, .y1 = 220});
    EXPECT_EQ(viewport.scale().cellSize, 20);
    EXPECT_EQ(text(viewport.offset()), "(2000, 3900)");
    EXPECT_EQ(text(viewport.visibleCells()), "[100, 140) x [195, 225)");

    // In a corner, clamping keeps the view inside the world: the cells are shown, not centred.
    viewport.fitCells({.x0 = 0, .y0 = 0, .x1 = 10, .y1 = 10});
    EXPECT_EQ(viewport.scale().cellSize, 60);  // min(800 / 10, 600 / 10)
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");

    // Cells that do not fit at 1 px: 1/2 px, where they are 500 × 500 pixels, as fitWorld() does.
    viewport.fitCells({.x0 = 0, .y0 = 0, .x1 = 1000, .y1 = 1000});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 1}));
    EXPECT_EQ(text(viewport.offset()), "(-150, -50)");

    // Below 1 px, pixels count as they are aligned. At 1/2 px, the 520 cells [255, 775) touch 261
    // pixels, 127 to 387, one more than the canvas is wide, so they need 1/4 px.
    Viewport narrow =
        makeViewport({.width = 10'000, .height = 10'000}, {.width = 260, .height = 600}, 4);
    narrow.fitCells({.x0 = 255, .y0 = 0, .x1 = 775, .y1 = 10});
    EXPECT_EQ(narrow.scale(), (Scale{.shrink = 2}));
    EXPECT_LE(narrow.visibleCells().x0, 255);
    EXPECT_GE(narrow.visibleCells().x1, 775);
    Viewport whole =
        makeViewport({.width = 1000, .height = 1000}, {.width = 800, .height = 600}, 4);
    whole.fitWorld();
    EXPECT_EQ(whole.scale().cellSize, viewport.scale().cellSize);
    EXPECT_EQ(text(whole.offset()), text(viewport.offset()));
}

TEST(ViewportTest, CenterOnPutsTheCellInTheMiddle)
{
    Viewport viewport =
        makeViewport({.width = 1000, .height = 1000}, {.width = 200, .height = 100}, 10);
    viewport.centerOn({.x = 500, .y = 500});
    EXPECT_EQ(text(viewport.offset()), "(4905, 4955)");  // 500 * 10 + 5 - canvas / 2
    EXPECT_EQ(text(viewport.cellAt({100, 50})), "(500, 500)");

    viewport.centerOn({.x = 0, .y = 0});  // clamped at the top-left corner
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");
    viewport.centerOn({.x = 999, .y = 999});  // and at the bottom-right corner
    EXPECT_EQ(text(viewport.offset()), "(9800, 9900)");
}

TEST(ViewportTest, ZoomByWalksTheTable)
{
    Viewport viewport =
        makeViewport({.width = 100, .height = 100}, {.width = 400, .height = 400}, 4);
    viewport.zoomBy(1, {});
    EXPECT_EQ(viewport.scale().cellSize, 5);
    viewport.zoomBy(3, {});
    EXPECT_EQ(viewport.scale().cellSize, 10);
    viewport.zoomBy(-2, {});
    EXPECT_EQ(viewport.scale().cellSize, 6);
    viewport.zoomBy(0, {});
    EXPECT_EQ(viewport.scale().cellSize, 6);

    viewport.zoomBy(100, {});  // clamped to the ends of the table
    EXPECT_EQ(viewport.scale().cellSize, 100);
    viewport.zoomBy(1, {});
    EXPECT_EQ(viewport.scale().cellSize, 100);
    viewport.zoomBy(std::numeric_limits<int>::min(), {});
    EXPECT_EQ(viewport.scale().cellSize, 1);
    viewport.zoomBy(-1, {});
    EXPECT_EQ(viewport.scale().cellSize, 1);
    viewport.zoomBy(std::numeric_limits<int>::max(), {});
    EXPECT_EQ(viewport.scale().cellSize, 100);
}

TEST(ViewportTest, ZoomByFromSizesNotInTheTable)
{
    // Steps count from the current size: the first step goes to the neighbouring entry.
    const auto zoomedFrom = [](int cellSize, int steps) {
        Viewport viewport =
            makeViewport({.width = 100, .height = 100}, {.width = 400, .height = 400}, cellSize);
        viewport.zoomBy(steps, {});
        return viewport.scale().cellSize;
    };
    EXPECT_EQ(zoomedFrom(7, 1), 8);
    EXPECT_EQ(zoomedFrom(7, -1), 6);
    EXPECT_EQ(zoomedFrom(7, 2), 10);
    EXPECT_EQ(zoomedFrom(7, -2), 5);
    EXPECT_EQ(zoomedFrom(9, 1), 10);
    EXPECT_EQ(zoomedFrom(9, -1), 8);
    EXPECT_EQ(zoomedFrom(15, 1), 16);
    EXPECT_EQ(zoomedFrom(15, -1), 12);
    EXPECT_EQ(zoomedFrom(99, 1), 100);
    EXPECT_EQ(zoomedFrom(99, -1), 80);
    EXPECT_EQ(zoomedFrom(99, 0), 99);
}

TEST(ViewportTest, ZoomByIsAnchored)
{
    Viewport viewport =
        makeViewport({.width = 10'000, .height = 10'000}, {.width = 800, .height = 600}, 7);
    viewport.centerOn({.x = 5000, .y = 5000});
    const PixelPoint                       anchor{.x = 123, .y = 456};
    const std::optional<core::UniversePos> before = viewport.cellAt(anchor);
    viewport.zoomBy(4, anchor);
    EXPECT_EQ(viewport.scale().cellSize, 16);
    EXPECT_EQ(text(viewport.cellAt(anchor)), text(before));
    viewport.zoomBy(-6, anchor);
    EXPECT_EQ(viewport.scale().cellSize, 4);
    EXPECT_EQ(text(viewport.cellAt(anchor)), text(before));
}

TEST(ViewportTest, LargestWorldAtLargestCellSize)
{
    const Coord     side = core::kMaxWorldSide;
    constexpr Pixel kFar = std::numeric_limits<Pixel>::max();
    Viewport        viewport =
        makeViewport({.width = side, .height = side}, {.width = 1920, .height = 1080}, kMaxCellSize,
                     {.x = kFar, .y = kFar});
    EXPECT_EQ(viewport.contentSize(), (PixelSize{10'000'000, 10'000'000}));
    EXPECT_EQ(text(viewport.offset()), "(9998080, 9998920)");
    EXPECT_EQ(text(viewport.cellAt({1919, 1079})), "(99999, 99999)");
    EXPECT_EQ(text(viewport.cellAt({1920, 1080})), "outside");
    EXPECT_EQ(text(viewport.cellOrigin({99'999, 99'999})), "(1820, 980)");
    EXPECT_EQ(text(viewport.visibleCells()), "[99980, 100000) x [99989, 100000)");
    EXPECT_EQ(text(viewport.cellAtClamped({1'000'000'000'000'000, -1'000'000'000'000'000})),
              "(99999, 0)");

    // Zooming out at the bottom-right corner keeps the last cell there.
    viewport.setCellSize(kMinCellSize, {.x = 1919, .y = 1079});
    EXPECT_EQ(text(viewport.offset()), "(98080, 98920)");
    EXPECT_EQ(text(viewport.cellAt({1919, 1079})), "(99999, 99999)");
}

TEST(ViewportTest, NoOverflowForTheLargestCoordinates)
{
    // Far beyond the world limits: every product still fits in 64 bits.
    constexpr Coord kMax     = std::numeric_limits<Coord>::max();
    Viewport        viewport = makeViewport({.width = kMax, .height = kMax},
                                            {.width = 1920, .height = 1080}, kMaxCellSize);
    viewport.scrollTo(
        {.x = std::numeric_limits<Pixel>::max(), .y = std::numeric_limits<Pixel>::max()});
    EXPECT_EQ(viewport.contentSize(), (PixelSize{Pixel{kMax} * 100, Pixel{kMax} * 100}));
    EXPECT_EQ(text(viewport.cellAt({1919, 1079})), std::format("({0}, {0})", kMax - 1));
    EXPECT_EQ(viewport.visibleCells().x1, kMax);
    EXPECT_EQ(viewport.visibleCells().y1, kMax);
    viewport.centerOn({.x = kMax - 100, .y = 0});
    EXPECT_EQ(text(viewport.cellAt({960, 0})), std::format("({}, 0)", kMax - 100));
}

TEST(ViewportTest, EmptyCanvasShowsNothing)
{
    for (const PixelSize canvas :
         {PixelSize{.width = 0, .height = 0}, PixelSize{.width = 0, .height = 100},
          PixelSize{.width = 100, .height = 0}})
    {
        const Viewport viewport = makeViewport({.width = 10, .height = 10}, canvas, 4);
        EXPECT_EQ(text(viewport.visibleCells()), "empty") << canvas.width << "x" << canvas.height;
    }
}

TEST(ViewportTest, NegativeCanvasSidesCountAsZero)
{
    const Viewport viewport =
        makeViewport({.width = 10, .height = 10}, {.width = -5, .height = -7}, 4);
    EXPECT_EQ(viewport.canvasSize(), (PixelSize{0, 0}));
    EXPECT_EQ(text(viewport.visibleCells()), "empty");
}

TEST(ViewportTest, AnUnboundedWorldHasNoEdges)
{
    Viewport viewport;
    viewport.setUnbounded();
    viewport.setCanvasSize({.width = 200, .height = 100});
    viewport.setCellSize(10, {});
    EXPECT_TRUE(viewport.unbounded());
    EXPECT_EQ(viewport.worldExtent(), (core::Extent{}));

    // Anywhere is a cell, negative coordinates included, and nothing is clipped.
    viewport.centerOn({.x = -1000, .y = 5'000'000});
    EXPECT_EQ(text(viewport.cellAt({100, 50})), "(-1000, 5000000)");
    EXPECT_EQ(text(viewport.cellAt({-5000, -5000})), "(-1510, 4999495)");
    EXPECT_EQ(text(viewport.cellAtClamped({-5000, -5000})), "(-1510, 4999495)");
    // The half-visible cells at both ends count too.
    EXPECT_EQ(text(viewport.visibleCells()), "[-1010, -989) x [4999995, 5000006)");

    // Fitting far-away cells works as anywhere else.
    constexpr core::UniverseCoord kFar = core::UniverseCoord{1} << 40;
    viewport.fitCells({.x0 = kFar, .y0 = -kFar, .x1 = kFar + 20, .y1 = -kFar + 10});
    EXPECT_EQ(viewport.scale().cellSize, 10);
    EXPECT_EQ(text(viewport.cellAt({100, 50})),
              text(core::UniversePos{.x = kFar + 10, .y = -kFar + 5}));
    // fitWorld() has no whole to fit and keeps the view.
    const PixelPoint before = viewport.offset();
    viewport.fitWorld();
    EXPECT_EQ(text(viewport.offset()), text(before));
}

TEST(ViewportTest, AnUnboundedViewStopsAtItsReach)
{
    Viewport viewport;
    viewport.setUnbounded();
    viewport.setCanvasSize({.width = 200, .height = 100});
    viewport.setCellSize(100, {});
    // Beyond the reach, so the view stops there instead of overflowing: at the limit, or within a
    // cell of it, since centring works in whole cells.
    viewport.centerOn({.x = core::UniverseCoord{1} << 60, .y = -(core::UniverseCoord{1} << 60)});
    EXPECT_EQ(viewport.offset().x, Viewport::kUnboundedReach - 200);
    EXPECT_GE(viewport.offset().y, -Viewport::kUnboundedReach);
    EXPECT_LT(viewport.offset().y, -Viewport::kUnboundedReach + 100);
    viewport.panBy(Viewport::kUnboundedReach, 0);
    EXPECT_EQ(viewport.offset().x, Viewport::kUnboundedReach - 200);
    // Zooming at the far edge stays exact and inside the reach.
    viewport.zoomBy(-5, {.x = 0, .y = 0});
    EXPECT_LE(viewport.offset().x, Viewport::kUnboundedReach - 200);

    // A fixed-size world again: clamped into it at once.
    viewport.setWorldExtent({.width = 10, .height = 10});
    EXPECT_FALSE(viewport.unbounded());
    EXPECT_EQ(text(viewport.cellAt({-1, -1})), "outside");
}

TEST(ViewportTest, ScalesOrderAndRead)
{
    EXPECT_LT((Scale{.shrink = 2}), (Scale{.shrink = 1}));
    EXPECT_LT((Scale{.shrink = 1}), (Scale{.cellSize = 1}));
    EXPECT_LT((Scale{.cellSize = 1}), (Scale{.cellSize = 2}));
    EXPECT_EQ(toString(Scale{.cellSize = 4}), "4 px");
    EXPECT_EQ(toString(Scale{.shrink = 4}), "1/16 px");
    EXPECT_EQ(toString(Scale{.shrink = 10}), "1/1,024 px");
    EXPECT_EQ(toString(Scale{.shrink = 11}), "1/2^11 px");
}

TEST(ViewportTest, ZoomingOutStopsOnceTheWholeWorldIsInView)
{
    // 10,000 cells across 800 pixels: 1/16 px (625 pixels) is the first scale that shows them all.
    Viewport viewport =
        makeViewport({.width = 10'000, .height = 3000}, {.width = 800, .height = 600}, 1);
    EXPECT_EQ(viewport.maxShrink(), 4U);
    viewport.zoomBy(-1, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 1}));
    viewport.zoomBy(-100, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 4}));
    EXPECT_EQ(text(viewport.visibleCells()), "[0, 10000) x [0, 3000)");
    viewport.setScale({.shrink = 30}, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 4}));
    // The ladder goes on up through the table.
    viewport.zoomBy(5, {});
    EXPECT_EQ(viewport.scale(), (Scale{.cellSize = 2}));

    // A world that fits at 1 px does not zoom out at all.
    Viewport small = makeViewport({.width = 500, .height = 500}, {.width = 800, .height = 600}, 1);
    EXPECT_EQ(small.maxShrink(), 0U);
    small.zoomBy(-1, {});
    EXPECT_EQ(small.scale(), (Scale{.cellSize = 1}));

    // After the canvas has grown, a view zoomed out beyond the new limit may zoom in, not out.
    viewport.setScale({.shrink = 4}, {});
    viewport.setCanvasSize({.width = 1600, .height = 1200});
    EXPECT_EQ(viewport.maxShrink(), 3U);
    viewport.zoomBy(-1, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 4}));
    viewport.zoomBy(1, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 3}));
}

TEST(ViewportTest, BelowOnePixelEachPixelShowsABlock)
{
    // Checked by trying every cell: canvas pixel p shows the cells c with c / 2^shrink, rounded
    // down, equal to offset + p. cellAt() is the first of them, and cellOrigin() maps it back.
    core::SplitMix64 rng(3);
    int              checked = 0;
    for (int i = 0; i < 300; ++i)
    {
        const Extent    world{.width  = static_cast<Coord>(pick(rng, 1, 3000)),
                              .height = static_cast<Coord>(pick(rng, 1, 3000))};
        const PixelSize canvas{.width = pick(rng, 1, 200), .height = pick(rng, 1, 150)};
        const Viewport  viewport =
            makeViewport(world, canvas, Scale{.shrink = static_cast<unsigned>(pick(rng, 1, 6))},
                         {.x = pick(rng, -50, 3000), .y = pick(rng, -50, 3000)});
        const Scale scale = viewport.scale();
        if (!scale.zoomedOut())
        {
            continue;  // the world fits at 1 px
        }
        ++checked;
        const std::int64_t perPixel = scale.cellsPerPixel();
        SCOPED_TRACE(std::format("world {}x{}, canvas {}x{}, {}, offset {}", world.width,
                                 world.height, canvas.width, canvas.height, toString(scale),
                                 text(viewport.offset())));

        const Span xs =
            bruteForceVisibleBlocks(viewport.offset().x, canvas.width, world.width, perPixel);
        const Span ys =
            bruteForceVisibleBlocks(viewport.offset().y, canvas.height, world.height, perPixel);
        EXPECT_EQ(
            text(viewport.visibleCells()),
            text(core::UniverseRect{.x0 = xs.first, .y0 = ys.first, .x1 = xs.end, .y1 = ys.end}));

        for (int j = 0; j < 20; ++j)
        {
            const PixelPoint           p{.x = pick(rng, 0, canvas.width - 1),
                                         .y = pick(rng, 0, canvas.height - 1)};
            const std::optional<Coord> x =
                bruteForceFirstCell(viewport.offset().x + p.x, world.width, perPixel);
            const std::optional<Coord> y =
                bruteForceFirstCell(viewport.offset().y + p.y, world.height, perPixel);
            const std::optional<core::UniversePos> expected =
                x && y ? std::optional(core::UniversePos{.x = *x, .y = *y}) : std::nullopt;
            EXPECT_EQ(text(viewport.cellAt(p)), text(expected)) << text(p);
            if (expected)
            {
                EXPECT_EQ(text(viewport.cellOrigin(*expected)), text(p));
            }
        }
    }
    EXPECT_GT(checked, 100);
}

TEST(ViewportTest, ZoomingBelowOnePixelAndBackRestoresTheOffset)
{
    // Down the ladder to 1/64 px and back up, or to and fro across 1 px, or between two levels
    // below it: every scale comes back to its offset. The view is on the middle of a large world,
    // so nothing is clamped on the way, wherever the anchor is.
    for (const int size : {1, 2, 8, 16, 100})
    {
        const int toOnePixel = static_cast<int>(nearestZoomStep(size));  // steps down to 1 px
        for (const PixelPoint anchor : {PixelPoint{.x = 0, .y = 0}, PixelPoint{.x = 101, .y = 73},
                                        PixelPoint{.x = 199, .y = 149}})
        {
            // Each walk moves |walk| steps (in if positive), one step at a time.
            const auto offsetAfter = [&](const std::vector<int>& walks) {
                Viewport viewport = makeViewport({.width = 100'000, .height = 100'000},
                                                 {.width = 200, .height = 150}, size);
                viewport.centerOn({.x = 50'000, .y = 50'000});
                for (const int walk : walks)
                {
                    for (int i = 0; i < std::abs(walk); ++i)
                    {
                        viewport.zoomBy(walk > 0 ? 1 : -1, anchor);
                    }
                }
                EXPECT_EQ(viewport.scale(), (Scale{.cellSize = size}));
                return text(viewport.offset());
            };
            SCOPED_TRACE(std::format("from {} px at {}", size, text(anchor)));
            const std::string home = offsetAfter({});
            EXPECT_EQ(offsetAfter({-(toOnePixel + 6), toOnePixel + 6}), home) << "down and up";
            std::vector<int> acrossOnePixel{-(toOnePixel + 1)};
            std::vector<int> belowOnePixel{-(toOnePixel + 3)};
            for (int i = 0; i < 10; ++i)
            {
                acrossOnePixel.insert(acrossOnePixel.end(), {1, -1});
                belowOnePixel.insert(belowOnePixel.end(), {-1, 1});
            }
            acrossOnePixel.push_back(toOnePixel + 1);
            belowOnePixel.push_back(toOnePixel + 3);
            EXPECT_EQ(offsetAfter(acrossOnePixel), home) << "across 1 px";
            EXPECT_EQ(offsetAfter(belowOnePixel), home) << "between 1/8 and 1/16 px";
        }
    }
}

TEST(ViewportTest, AnUnboundedViewZoomsOutToTheWholeUniverse)
{
    Viewport viewport;
    viewport.setUnbounded();
    viewport.setCanvasSize({.width = 1000, .height = 600});
    viewport.setCellSize(1, {});
    // The universe is 2^62 cells across; at 2^53 cells a pixel it is 512 pixels, which fit.
    EXPECT_EQ(viewport.maxShrink(), 53U);
    const PixelPoint centre{.x = 500, .y = 300};
    viewport.zoomBy(-1000, centre);
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = 53}));
    // Centred, with nothing beyond its edges.
    EXPECT_EQ(text(viewport.offset()), "(-500, -300)");
    constexpr core::UniverseCoord kEdge = core::kUniverseRadius;
    EXPECT_EQ(text(viewport.visibleCells()),
              text(core::UniverseRect{.x0 = -kEdge, .y0 = -kEdge, .x1 = kEdge, .y1 = kEdge}));
    EXPECT_EQ(text(viewport.cellAt({.x = 0, .y = 0})), "outside");
    EXPECT_EQ(text(viewport.cellAt(centre)), "(0, 0)");
    EXPECT_EQ(text(viewport.cellAt({.x = 244, .y = 44})),
              text(core::UniversePos{.x = -kEdge, .y = -kEdge}));
    // And all the way back in, to where it started.
    viewport.zoomBy(53, centre);
    EXPECT_EQ(viewport.scale(), (Scale{.cellSize = 1}));
    EXPECT_EQ(text(viewport.offset()), "(0, 0)");

    // A one-pixel canvas stops at kMaxShrink, where the universe is 4 pixels wide, without
    // overflowing anywhere.
    viewport.setCanvasSize({.width = 1, .height = 1});
    viewport.zoomBy(-1000, {});
    EXPECT_EQ(viewport.scale(), (Scale{.shrink = Viewport::kMaxShrink}));
    viewport.panBy(-100, -100);
    EXPECT_EQ(text(viewport.cellAt({})), text(core::UniversePos{.x = -kEdge, .y = -kEdge}));
    viewport.panBy(100, 100);
    EXPECT_EQ(text(viewport.visibleCells()),
              text(core::UniverseRect{.x0 = kEdge / 2, .y0 = kEdge / 2, .x1 = kEdge, .y1 = kEdge}));
}

}  // namespace
}  // namespace wxLife::render
