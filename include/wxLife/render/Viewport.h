#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "wxLife/core/Types.h"
#include "wxLife/render/Types.h"

namespace wxLife::render
{

inline constexpr int        kMinCellSize = 1;
inline constexpr int        kMaxCellSize = 100;
inline constexpr std::array kZoomSteps{1,  2,  3,  4,  5,  6,  8,  10, 12,
                                       16, 20, 25, 32, 40, 50, 64, 80, 100};

/// Index of the kZoomSteps entry closest to `cellSize` (ties go to the smaller entry).
[[nodiscard]] constexpr std::size_t nearestZoomStep(int cellSize) noexcept
{
    const auto distance = [cellSize](int step) {
        const std::int64_t d = std::int64_t{step} - cellSize;  // 64-bit: cellSize may be any int
        return d < 0 ? -d : d;
    };
    // min_element returns the first of equal entries, and the table is sorted, so the smaller wins.
    // (An index, as in findPreset(): std::array's iterator is a class in MSVC's library.)
    return static_cast<std::size_t>(std::ranges::distance(
        kZoomSteps.begin(), std::ranges::min_element(kZoomSteps, {}, distance)));
}

/// "4 px", "1/16 px", and from 1/2^11 on "1/2^11 px".
[[nodiscard]] std::string toString(Scale scale);

/// The camera: maps canvas device pixels to world cells and back.
/// offset() is the content pixel shown at the canvas's top-left corner. Content pixel p shows cell
/// floor(p / cellSize), or, zoomed out below 1 px, the block of cells [p × 2^shrink,
/// (p + 1) × 2^shrink) on that axis. Every change clamps the offset: on an axis where the world is
/// smaller than the canvas the world is centred (a negative offset), otherwise the view stays
/// inside the world. An unbounded world's view stays within kUnboundedReach content pixels of its
/// centre and within the universe (core::kUniverseRadius).
class Viewport
{
public:
    /// How far the view of an unbounded world may go from its centre, in content pixels. It keeps
    /// every product in the zoom arithmetic within 64 bits; at 1 px per cell it is 3.6e16 cells.
    static constexpr Pixel kUnboundedReach = Pixel{1} << 55;
    /// The furthest any view zooms out: 2^60 cells per pixel, where the universe is 4 px wide.
    static constexpr unsigned kMaxShrink = 60;

    /// A fixed-size world, (0, 0) its top-left cell.
    void setWorldExtent(core::Extent world) noexcept;
    /// An unbounded world, with no edges but the universe's.
    void setUnbounded() noexcept;
    /// Negative sides count as 0.
    void setCanvasSize(PixelSize canvas) noexcept;

    [[nodiscard]] bool unbounded() const noexcept;
    /// {} when unbounded.
    [[nodiscard]] core::Extent worldExtent() const noexcept;
    [[nodiscard]] PixelSize    canvasSize() const noexcept;
    [[nodiscard]] Scale        scale() const noexcept;
    [[nodiscard]] PixelPoint   offset() const noexcept;
    /// The world in content pixels; {} when unbounded.
    [[nodiscard]] PixelSize contentSize() const noexcept;
    /// How far zooming out may go: until the whole world, for an unbounded one the universe, fits
    /// the canvas. 0 when the world already fits at 1 px.
    [[nodiscard]] unsigned maxShrink() const noexcept;

    /// Changes the scale. The world point under the centre of the `anchor` pixel stays inside
    /// that pixel, and so does the cell under the anchor, unless clamping moves the view. On each
    /// axis, zooms at the same anchor with no other camera change in between form a run that keeps
    /// the point of its first zoom: rounding errors never add up, and going back to a scale
    /// restores its offset. Clamping an axis (centring included) ends that axis's run only.
    /// @param scale Its cell size is clamped to [kMinCellSize, kMaxCellSize], and its shrink to
    ///              maxShrink(); a view already zoomed out further stays where it is.
    void setScale(Scale scale, PixelPoint anchor) noexcept;
    /// setScale() with `px` pixels per cell.
    void setCellSize(int px, PixelPoint anchor) noexcept;
    /// Moves `steps` places up (steps > 0) or down along the zoom ladder: the shrink levels from
    /// maxShrink() to 1, then the kZoomSteps entries. Anchored like setScale().
    void zoomBy(int steps, PixelPoint anchor) noexcept;
    /// Largest scale that shows the whole world; centres it. An unbounded world has no whole, so
    /// the view keeps its place.
    void fitWorld() noexcept;
    /// Largest scale that shows every cell of `cells`, and centres them as far as the clamping
    /// allows. An empty side does not limit the scale, and an empty canvas gets 1 px.
    void fitCells(core::UniverseRect cells) noexcept;
    void centerOn(core::UniversePos cell) noexcept;
    /// Positive values move the view right/down.
    void panBy(Pixel dx, Pixel dy) noexcept;
    void scrollTo(PixelPoint offset) noexcept;

    /// The cell under a canvas pixel: below 1 px, the first cell of the pixel's block.
    /// @return nullopt when the point is outside the world, or outside an unbounded world's reach.
    [[nodiscard]] std::optional<core::UniversePos> cellAt(PixelPoint canvasPoint) const noexcept;
    /// Like cellAt(), but clamped to the world; for drags that leave it.
    [[nodiscard]] core::UniversePos cellAtClamped(PixelPoint canvasPoint) const noexcept;
    /// Canvas pixel of the cell's top-left corner, or below 1 px of the pixel that shows it.
    /// @pre the cell is within the reach
    [[nodiscard]] PixelPoint cellOrigin(core::UniversePos cell) const noexcept;
    /// Every cell a canvas pixel shows, clipped to the world or the reach. Empty (test it with
    /// empty()) only for an empty canvas.
    [[nodiscard]] core::UniverseRect visibleCells() const noexcept;

private:
    /// One axis of a run of zooms: the world point it keeps under `anchor`, which is `cell` plus
    /// `numerator / denominator` of a cell. The run lasts while the axis is where its last zoom
    /// left it (`offset`, `scale`).
    struct ZoomRun
    {
        Pixel               anchor      = 0;
        core::UniverseCoord cell        = 0;
        Pixel               numerator   = 0;
        Pixel               denominator = 1;
        Pixel               offset      = 0;
        Scale               scale{};
    };

    /// The offset along one axis that puts the run's point under `anchor` at scale `to`. Starts a
    /// new run in `run` unless the current one goes on.
    [[nodiscard]] static Pixel zoomedOffset(std::optional<ZoomRun>& run, Pixel offset, Pixel anchor,
                                            Scale from, Scale to) noexcept;
    /// The world along one axis in content pixels, [first, second): [0, side) scaled for a
    /// fixed-size world, the reach either side of 0 for an unbounded one.
    [[nodiscard]] std::pair<Pixel, Pixel> contentSpan(core::Coord side, Scale scale) const noexcept;

    void clampOffset() noexcept;

    core::Extent                          m_world{};
    bool                                  m_unbounded = false;
    PixelSize                             m_canvas{};
    Scale                                 m_scale{.cellSize = 4};
    PixelPoint                            m_offset{};
    std::array<std::optional<ZoomRun>, 2> m_zoomRuns;  ///< x and y
};

}  // namespace wxLife::render
