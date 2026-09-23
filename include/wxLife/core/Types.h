#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace wxLife::core
{

using Cell      = std::uint8_t;  ///< 0 = dead, 1 = alive. One byte keeps neighbour sums trivial.
using Coord     = std::int32_t;  ///< Cell coordinate or side length (at most kMaxWorldSide).
using CellCount = std::int64_t;  ///< Areas and populations, which can exceed 2^31.

inline constexpr Cell kDead  = 0;
inline constexpr Cell kAlive = 1;

/// How the world treats its edges. The core branches on it only in switches without `default`, so
/// -Wswitch lists every place there that a new topology needs.
enum class Topology : std::uint8_t
{
    BOUNDED,  ///< Cells beyond the edge are always dead.
    TORUS,    ///< Opposite edges are neighbours.
};

/// Every Topology, so tests cover a new one automatically.
inline constexpr std::array kTopologies{Topology::BOUNDED, Topology::TORUS};

[[nodiscard]] constexpr std::string_view toString(Topology t) noexcept
{
    switch (t)
    {
        case Topology::BOUNDED:
            return "bounded";
        case Topology::TORUS:
            return "torus";
    }
    std::unreachable();
}

/// Which automaton the world runs. The core branches on it only in switches without `default`, so
/// -Wswitch lists every place there that a new automaton needs.
enum class Automaton : std::uint8_t
{
    LIFE,         ///< A two-state B/S rule, stepped by a Stepper.
    LANGTON_ANT,  ///< Langton's ant: the cells are its tape, and the ants are the only movers.
};

/// Every Automaton, so tests cover a new one automatically.
inline constexpr std::array kAutomata{Automaton::LIFE, Automaton::LANGTON_ANT};

[[nodiscard]] constexpr std::string_view toString(Automaton a) noexcept
{
    switch (a)
    {
        case Automaton::LIFE:
            return "Life";
        case Automaton::LANGTON_ANT:
            return "Langton's ant";
    }
    std::unreachable();
}

/// What a world is. The core branches on it only in switches without `default`.
enum class WorldKind : std::uint8_t
{
    FIXED_SIZE,  ///< A grid of so many cells, with dead or wrapping edges.
    UNBOUNDED,   ///< An unbounded plane, run by HashLife.
};

/// Every WorldKind, so tests cover a new one automatically.
inline constexpr std::array kWorldKinds{WorldKind::FIXED_SIZE, WorldKind::UNBOUNDED};

[[nodiscard]] constexpr std::string_view toString(WorldKind k) noexcept
{
    switch (k)
    {
        case WorldKind::FIXED_SIZE:
            return "fixed size";
        case WorldKind::UNBOUNDED:
            return "unbounded";
    }
    std::unreachable();
}

/// Cell position; (0, 0) is the top-left cell.
struct CellPos
{
    Coord x = 0;
    Coord y = 0;

    friend constexpr bool operator==(CellPos, CellPos) noexcept = default;
};

/// Width and height of a world, in cells.
struct Extent
{
    Coord width  = 0;
    Coord height = 0;

    [[nodiscard]] constexpr CellCount cellCount() const noexcept
    {
        return CellCount{width} * height;
    }
    [[nodiscard]] constexpr bool contains(CellPos p) const noexcept
    {
        return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height;
    }

    friend constexpr bool operator==(Extent, Extent) noexcept = default;
};

/// Half-open cell rectangle [x0, x1) × [y0, y1).
struct CellRect
{
    Coord x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return x0 >= x1 || y0 >= y1; }

    friend constexpr bool operator==(CellRect, CellRect) noexcept = default;
};

/// A 64-bit cell coordinate: on an unbounded plane (0, 0) is its centre, in a fixed-size world the
/// top-left cell.
using UniverseCoord = std::int64_t;

/// The cells of an unbounded plane lie in [-kUniverseRadius, kUniverseRadius) on each axis, so a
/// view of one never needs to go further.
inline constexpr UniverseCoord kUniverseRadius = UniverseCoord{1} << 61;

/// A cell position with 64-bit coordinates, for any kind of world.
struct UniversePos
{
    UniverseCoord x = 0;
    UniverseCoord y = 0;

    friend constexpr bool operator==(UniversePos, UniversePos) noexcept = default;
};

/// Half-open cell rectangle [x0, x1) × [y0, y1) with 64-bit coordinates.
struct UniverseRect
{
    UniverseCoord x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return x0 >= x1 || y0 >= y1; }

    friend constexpr bool operator==(UniverseRect, UniverseRect) noexcept = default;
};

/// Division rounding toward negative infinity. @pre b > 0
[[nodiscard]] constexpr std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept
{
    const std::int64_t q = a / b;  // rounds toward zero
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

}  // namespace wxLife::core
