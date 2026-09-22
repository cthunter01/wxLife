/// @file
/// Langton's ant: which way an ant faces, the two-state turn rule, and where an ant starts.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "wxLife/core/Grid.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Ants a world may carry. Enough to watch them collide, and few enough that one generation stays
/// trivial.
inline constexpr int kMaxAnts = 64;

/// Which way an ant faces. Clockwise, so a turn is arithmetic on the value.
enum class Heading : std::uint8_t
{
    NORTH,
    EAST,
    SOUTH,
    WEST
};

/// Every Heading, so tests cover a new one automatically.
inline constexpr std::array kHeadings{Heading::NORTH, Heading::EAST, Heading::SOUTH, Heading::WEST};

[[nodiscard]] constexpr std::string_view toString(Heading h) noexcept
{
    switch (h)
    {
        case Heading::NORTH:
            return "north";
        case Heading::EAST:
            return "east";
        case Heading::SOUTH:
            return "south";
        case Heading::WEST:
            return "west";
    }
    std::unreachable();
}

[[nodiscard]] constexpr Heading turnRight(Heading h) noexcept
{
    return static_cast<Heading>((std::to_underlying(h) + 1) % 4);
}

[[nodiscard]] constexpr Heading turnLeft(Heading h) noexcept
{
    return static_cast<Heading>((std::to_underlying(h) + 3) % 4);
}

/// The cell one step ahead. It always wraps, whatever the world's Topology: an ant that walked off
/// a bounded edge would have to be deleted, while a wrapped one keeps drawing.
/// @pre extent.contains(p), and both sides are at least 1
[[nodiscard]] constexpr CellPos forward(CellPos p, Heading h, Extent extent) noexcept
{
    switch (h)
    {
        case Heading::NORTH:
            return {.x = p.x, .y = p.y > 0 ? p.y - 1 : extent.height - 1};
        case Heading::EAST:
            return {.x = p.x + 1 < extent.width ? p.x + 1 : 0, .y = p.y};
        case Heading::SOUTH:
            return {.x = p.x, .y = p.y + 1 < extent.height ? p.y + 1 : 0};
        case Heading::WEST:
            return {.x = p.x > 0 ? p.x - 1 : extent.width - 1, .y = p.y};
    }
    std::unreachable();
}

/// One ant: where it stands and where it faces.
struct Ant
{
    CellPos position{};
    Heading heading = Heading::NORTH;

    friend constexpr bool operator==(Ant, Ant) noexcept = default;
};

/// Where the i-th of `count` ants starts: evenly spaced along the middle row, facing north. One ant
/// therefore starts in the centre, where Langton's ant draws its classic pattern.
/// @pre 0 <= index < count, and both sides are at least 1
[[nodiscard]] constexpr Ant defaultAnt(int index, int count, Extent extent) noexcept
{
    // 64-bit before the cast, so the widest world cannot overflow.
    const auto x = static_cast<Coord>(CellCount{extent.width} * (index + 1) / (count + 1));
    return {.position = {.x = x, .y = extent.height / 2}, .heading = Heading::NORTH};
}

/// One move: on a dead cell the ant turns right, on a live one left; then it flips the cell and
/// steps forward, wrapping at every edge.
/// @pre grid.extent().contains(ant.position)
/// @return +1 when the cell came alive, -1 when it died: the change to the population.
inline CellCount advance(Ant& ant, Grid& grid) noexcept
{
    const bool alive = grid.at(ant.position) != kDead;
    ant.heading      = alive ? turnLeft(ant.heading) : turnRight(ant.heading);
    grid.set(ant.position, alive ? kDead : kAlive);
    ant.position = forward(ant.position, ant.heading, grid.extent());
    return alive ? -1 : 1;
}

}  // namespace wxLife::core
