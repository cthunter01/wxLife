/// @file
/// Bresenham line walk used for gap-free mouse strokes.
#pragma once

#include <concepts>
#include <cstdint>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Calls fn for every cell of the 8-connected line from a to b, both ends included, starting with
/// a. Swapping a and b visits the same cells in reverse order.
template <std::invocable<CellPos> Fn>
constexpr void forEachCellOnLine(CellPos a, CellPos b, Fn fn)
{
    // 64-bit throughout: b - a reaches 2^32 when the endpoints are near the int32 limits.
    const std::int64_t dx = std::int64_t{b.x} - a.x;
    const std::int64_t dy = std::int64_t{b.y} - a.y;
    // std::abs is not constexpr yet.
    const auto         magnitude = [](std::int64_t v) { return v < 0 ? -v : v; };
    const bool         xIsMajor  = magnitude(dx) >= magnitude(dy);
    const std::int64_t major     = xIsMajor ? dx : dy;  // signed distance along the longer axis
    const std::int64_t minor     = xIsMajor ? dy : dx;
    const std::int64_t steps     = magnitude(major);  // the line has steps + 1 cells
    const std::int64_t majorStep = major < 0 ? -1 : 1;

    // After i steps the minor offset is floor(minor * i / steps + 1/2). Rounding halves the same
    // way in absolute coordinates is what makes both walking directions produce the same cells. q
    // and r are the quotient and remainder of (2 * minor * i + steps) / (2 * steps), kept
    // incrementally so that no product of two distances is ever formed.
    std::int64_t q = 0;
    std::int64_t r = steps;
    for (std::int64_t i = 0; i <= steps; ++i)
    {
        const std::int64_t along   = majorStep * i;
        const std::int64_t offsetX = xIsMajor ? along : q;
        const std::int64_t offsetY = xIsMajor ? q : along;
        fn(CellPos{.x = static_cast<Coord>(a.x + offsetX), .y = static_cast<Coord>(a.y + offsetY)});

        r += 2 * minor;  // |minor| <= steps, so one correction is enough
        if (r >= 2 * steps)
        {
            r -= 2 * steps;
            ++q;
        }
        else if (r < 0)
        {
            r += 2 * steps;
            --q;
        }
    }
}

}  // namespace wxLife::core
