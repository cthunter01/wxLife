/// @file
/// Macrocell patterns: the quadtree of Golly's .mc files, as readPattern() reads it and HashLife
/// loads it.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// One node of a macrocell quadtree: a leaf of 8 × 8 cells, or four children one level down.
struct MacrocellNode
{
    std::uint8_t  level = 3;  ///< The node is 2^level cells on a side; a leaf is level 3.
    std::uint64_t leaf  = 0;  ///< A leaf's live cells: bit y * 8 + x, (0, 0) its top-left cell.
    /// An inner node's children, NW, NE, SW, SE, as positions in Macrocell::nodes plus one, as the
    /// file numbers them; 0 is an empty child.
    std::array<std::uint32_t, 4> children{};
};

/// A pattern as a quadtree whose identical parts are shared, which is how a pattern of millions of
/// cells fits in a file of a few megabytes.
struct Macrocell
{
    /// Every node comes after its children, and the last one is the root, which is centred on
    /// (0, 0): a root of level L covers [-2^(L-1), 2^(L-1)) on each axis. Empty for no cells.
    std::vector<MacrocellNode> nodes;
    std::uint64_t              population = 0;
    /// The smallest rectangle that holds every live cell; empty when there is none.
    UniverseRect  bounds;
    std::uint64_t generation = 0;  ///< The file's "#G" line; 0 without one.
};

}  // namespace wxLife::core
