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
#include "wxLife/render/Viewport.h"

namespace wxLife::render
{
namespace
{

using core::CellPos;
using core::CellRect;
using core::Coord;
using core::Extent;

// Positions and rectangles are compared as text, so a failure prints readable values.
std::string text(PixelPoint p)
{
    return std::format("({}, {})", p.x, p.y);
}

std::string text(CellPos c)
{
    return std::format("({}, {})", c.x, c.y);
}

std::string text(std::optional<CellPos> c)
{
    return c ? text(*c) : "outside";
}

std::string text(CellRect r)
{
    return r.empty() ? "empty" : std::format("[{}, {}) x [{}, {})", r.x0, r.x1, r.y0, r.y1);
}

Viewport makeViewport(Extent world, PixelSize canvas, int cellSize, PixelPoint offset = {})
{
    Viewport viewport;
    viewport.setWorldExtent(world);
    viewport.setCanvasSize(canvas);
    viewport.setCellSize(cellSize, {});
    viewport.scrollTo(offset);
    return viewport;
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
        return (static_cast<double>(offset + canvas) + 0.5) / viewport.cellSize();
    };
    return {.x = axis(viewport.offset().x, p.x), .y = axis(viewport.offset().y, p.y)};
}

// `point` lies inside canvas pixel `p` (edges included): at most half a pixel from its centre.
void expectInsidePixel(const Viewport& viewport, PixelPoint p, WorldPoint point)
{
    const double     halfPixel = (0.5 / viewport.cellSize()) + 1e-9;  // 1e-9 absorbs rounding
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

// The cell whose pixel square contains the canvas point, found by trying every cell.
std::optional<CellPos> bruteForceCellAt(const Viewport& viewport, PixelPoint p)
{
    const int  size = viewport.cellSize();
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
    return x && y ? std::optional(CellPos{.x = *x, .y = *y}) : std::nullopt;
}

TEST(ViewportTest, StartsEmpty)
{
    const Viewport viewport;
    EXPECT_EQ(viewport.worldExtent(), (Extent{}));
    EXPECT_EQ(viewport.canvasSize(), (PixelSize{}));
    EXPECT_EQ(viewport.cellSize(), 4);
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
    EXPECT_EQ(viewport.cellSize(), kMinCellSize);
    viewport.setCellSize(101, {});
    EXPECT_EQ(viewport.cellSize(), kMaxCellSize);
    viewport.setCellSize(std::numeric_limits<int>::min(), {});
    EXPECT_EQ(viewport.cellSize(), kMinCellSize);
    viewport.setCellSize(37, {});  // any size in range is kept exactly
    EXPECT_EQ(viewport.cellSize(), 37);
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
                const std::optional<CellPos> cell  = viewport.cellAt(anchor);
                const WorldPoint             point = worldAt(viewport, anchor);
                ASSERT_TRUE(cell);

                viewport.setCellSize(to, anchor);
                SCOPED_TRACE(std::format("{} px -> {} px at {}", from, to, text(anchor)));
                // The test is only meaningful when the new offset was not clamped.
                const PixelSize content = viewport.contentSize();
                ASSERT_GT(viewport.offset().x, 0);
                ASSERT_GT(viewport.offset().y, 0);
                ASSERT_LT(viewport.offset().x, content.width - canvas.width);
                ASSERT_LT(viewport.offset().y, content.height - canvas.height);

                EXPECT_EQ(viewport.cellSize(), to);
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
        return core::floorDiv(viewport.offset().x + anchor.x, viewport.cellSize());
    };
    ASSERT_EQ(cellUnderAnchor(), -101);
    viewport.setCellSize(1, anchor);
    EXPECT_EQ(viewport.offset().x, 399);
    EXPECT_EQ(cellUnderAnchor(), -101);
}

TEST(ViewportTest, RepeatedZoomingDoesNotDrift)
{
    // Every zoom of a run keeps the run's first point, and each size always gets the same offset.
    core::SplitMix64 rng(7);
    Viewport         viewport =
        makeViewport({.width = 100'000, .height = 100'000}, {.width = 800, .height = 600}, 8);
    viewport.centerOn({.x = 31'415, .y = 27'182});
    const PixelPoint          anchor{.x = 313, .y = 207};
    const WorldPoint          point = worldAt(viewport, anchor);
    std::map<int, PixelPoint> offsets{{viewport.cellSize(), viewport.offset()}};
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
        SCOPED_TRACE(std::format("after {} zooms, at {} px", i + 1, viewport.cellSize()));
        expectInsidePixel(viewport, anchor, point);
        // The first visit of a size records its offset; later visits must match it.
        const PixelPoint expected =
            offsets.try_emplace(viewport.cellSize(), viewport.offset()).first->second;
        ASSERT_EQ(text(viewport.offset()), text(expected));
    }
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
                EXPECT_EQ(viewport.cellSize(), size);
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
    const PixelPoint             anchor{.x = 537, .y = 300};
    const std::optional<CellPos> cell = viewport.cellAt(anchor);
    ASSERT_TRUE(cell);
    while (viewport.cellSize() < kMaxCellSize)
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
    const PixelPoint             anchor{.x = 1000, .y = 500};
    const std::optional<CellPos> cell = viewport.cellAt(anchor);
    ASSERT_EQ(text(cell), "(50, 500)");
    while (viewport.cellSize() < kMaxCellSize)
    {
        viewport.zoomBy(1, anchor);
    }
    const std::optional<CellPos> now = viewport.cellAt(anchor);
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
        const std::optional<CellPos> cell = viewport.cellAt(anchor);
        viewport.zoomBy(1, anchor);
        EXPECT_EQ(text(viewport.cellAt(anchor)), text(cell)) << name;
    };
    check("panBy", [](Viewport& v) { v.panBy(37, -41); });
    check("scrollTo",
          [](Viewport& v) { v.scrollTo({.x = v.offset().x - 50, .y = v.offset().y + 70}); });
    check("centerOn", [](Viewport& v) { v.centerOn({.x = 4990, .y = 5010}); });
    check("fitWorld", [](Viewport& v) { v.fitWorld(); });
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
    const std::optional<CellPos> cell = edge.cellAt(nearEdge);
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
                << viewport.cellSize();
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
        const int        size = viewport.cellSize();
        const CellPos    cell{.x = static_cast<Coord>(pick(rng, 0, world.width - 1)),
                              .y = static_cast<Coord>(pick(rng, 0, world.height - 1))};
        const PixelPoint origin = viewport.cellOrigin(cell);
        const PixelPoint last{.x = origin.x + size - 1, .y = origin.y + size - 1};
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
        const Span      xs =
            bruteForceVisible(viewport.offset().x, canvas.width, world.width, viewport.cellSize());
        const Span     ys = bruteForceVisible(viewport.offset().y, canvas.height, world.height,
                                              viewport.cellSize());
        const CellRect expected{.x0 = xs.first, .y0 = ys.first, .x1 = xs.end, .y1 = ys.end};
        ASSERT_EQ(text(viewport.visibleCells()), text(expected))
            << "world " << world.width << "x" << world.height << ", canvas " << canvas.width << "x"
            << canvas.height << ", offset " << text(viewport.offset()) << ", cell size "
            << viewport.cellSize();
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
    EXPECT_EQ(viewport.cellSize(), 10);  // min(1000 / 100, 800 / 50)
    EXPECT_EQ(text(viewport.offset()), "(0, -150)");
    EXPECT_EQ(text(viewport.visibleCells()), "[0, 100) x [0, 50)");

    viewport.setWorldExtent({.width = 512, .height = 512});
    viewport.setCanvasSize({.width = 1200, .height = 800});
    viewport.fitWorld();
    EXPECT_EQ(viewport.cellSize(), 1);
    EXPECT_EQ(text(viewport.offset()), "(-344, -144)");
}

TEST(ViewportTest, FitWorldStopsAtTheLargestCellSize)
{
    Viewport viewport = makeViewport({.width = 5, .height = 5}, {.width = 1000, .height = 1000}, 4);
    viewport.fitWorld();
    EXPECT_EQ(viewport.cellSize(), kMaxCellSize);
    EXPECT_EQ(text(viewport.offset()), "(-250, -250)");
}

TEST(ViewportTest, FitWorldCentresAWorldTooLargeToFit)
{
    // Even at 1 px the world is larger than the canvas, so the view opens on its centre.
    Viewport viewport =
        makeViewport({.width = 20'000, .height = 20'000}, {.width = 1000, .height = 800}, 16);
    viewport.fitWorld();
    EXPECT_EQ(viewport.cellSize(), 1);
    EXPECT_EQ(text(viewport.offset()), "(9500, 9600)");
    EXPECT_EQ(text(viewport.cellAt({500, 400})), "(10000, 10000)");
}

TEST(ViewportTest, FitWorldHandlesEmptyInputs)
{
    Viewport noCanvas = makeViewport({.width = 10, .height = 10}, {.width = 0, .height = 0}, 50);
    noCanvas.fitWorld();
    EXPECT_EQ(noCanvas.cellSize(), kMinCellSize);
    EXPECT_EQ(text(noCanvas.visibleCells()), "empty");

    Viewport noWorld;
    noWorld.setCanvasSize({.width = 300, .height = 200});
    noWorld.fitWorld();
    EXPECT_EQ(noWorld.cellSize(), kMaxCellSize);
    EXPECT_EQ(text(noWorld.offset()), "(-150, -100)");
    EXPECT_EQ(text(noWorld.visibleCells()), "empty");
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
    EXPECT_EQ(viewport.cellSize(), 5);
    viewport.zoomBy(3, {});
    EXPECT_EQ(viewport.cellSize(), 10);
    viewport.zoomBy(-2, {});
    EXPECT_EQ(viewport.cellSize(), 6);
    viewport.zoomBy(0, {});
    EXPECT_EQ(viewport.cellSize(), 6);

    viewport.zoomBy(100, {});  // clamped to the ends of the table
    EXPECT_EQ(viewport.cellSize(), 100);
    viewport.zoomBy(1, {});
    EXPECT_EQ(viewport.cellSize(), 100);
    viewport.zoomBy(std::numeric_limits<int>::min(), {});
    EXPECT_EQ(viewport.cellSize(), 1);
    viewport.zoomBy(-1, {});
    EXPECT_EQ(viewport.cellSize(), 1);
    viewport.zoomBy(std::numeric_limits<int>::max(), {});
    EXPECT_EQ(viewport.cellSize(), 100);
}

TEST(ViewportTest, ZoomByFromSizesNotInTheTable)
{
    // Steps count from the current size: the first step goes to the neighbouring entry.
    const auto zoomedFrom = [](int cellSize, int steps) {
        Viewport viewport =
            makeViewport({.width = 100, .height = 100}, {.width = 400, .height = 400}, cellSize);
        viewport.zoomBy(steps, {});
        return viewport.cellSize();
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
    const PixelPoint             anchor{.x = 123, .y = 456};
    const std::optional<CellPos> before = viewport.cellAt(anchor);
    viewport.zoomBy(4, anchor);
    EXPECT_EQ(viewport.cellSize(), 16);
    EXPECT_EQ(text(viewport.cellAt(anchor)), text(before));
    viewport.zoomBy(-6, anchor);
    EXPECT_EQ(viewport.cellSize(), 4);
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

}  // namespace
}  // namespace wxLife::render
