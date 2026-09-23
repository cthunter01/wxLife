#pragma once

#include <compare>
#include <cstdint>

namespace wxLife::render
{

using Pixel = std::int64_t;  ///< Device pixels; 64-bit so cell × size products never overflow.

/// A point in device pixels.
struct PixelPoint
{
    Pixel x = 0;
    Pixel y = 0;

    friend constexpr bool operator==(PixelPoint, PixelPoint) noexcept = default;
};

/// A size in device pixels.
struct PixelSize
{
    Pixel width  = 0;
    Pixel height = 0;

    friend constexpr bool operator==(PixelSize, PixelSize) noexcept = default;
};

/// How large cells look on the canvas: `cellSize` device pixels per cell side, or, zoomed out below
/// 1 px, one pixel for each block of 2^shrink × 2^shrink cells (cellSize is then 1).
struct Scale
{
    int      cellSize = 1;
    unsigned shrink   = 0;

    [[nodiscard]] constexpr bool zoomedOut() const noexcept { return shrink > 0; }
    /// Cells per pixel along each side: 2^shrink.
    [[nodiscard]] constexpr std::int64_t cellsPerPixel() const noexcept
    {
        return std::int64_t{1} << shrink;
    }

    friend constexpr bool operator==(Scale, Scale) noexcept = default;
    /// Larger cells compare greater: 1/4 px < 1/2 px < 1 px < 2 px.
    friend constexpr std::strong_ordering operator<=>(Scale a, Scale b) noexcept
    {
        const auto key = [](Scale scale) {
            return scale.zoomedOut() ? -std::int64_t{scale.shrink} : std::int64_t{scale.cellSize};
        };
        return key(a) <=> key(b);
    }
};

/// One 24-bit colour.
struct Rgb
{
    std::uint8_t r = 0, g = 0, b = 0;

    friend constexpr bool operator==(Rgb, Rgb) noexcept = default;
};

}  // namespace wxLife::render
