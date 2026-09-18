#pragma once

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

/// One 24-bit colour.
struct Rgb
{
    std::uint8_t r = 0, g = 0, b = 0;

    friend constexpr bool operator==(Rgb, Rgb) noexcept = default;
};

}  // namespace wxLife::render
