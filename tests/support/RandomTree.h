/// @file
/// A test helper that makes large macrocell trees, for tests that need more distinct nodes than a
/// small memory budget holds.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "wxLife/core/Macrocell.h"
#include "wxLife/core/Random.h"

namespace wxLife::test
{

/// `leaves` leaves of random cells, joined four at a time, level by level, up to one root. Random
/// 8 × 8 leaves are nearly all different, and so are the 4 × 4 squares inside them.
[[nodiscard]] inline core::Macrocell randomTree(std::size_t leaves, std::uint64_t seed)
{
    core::Macrocell            tree;
    core::SplitMix64           random(seed);
    std::vector<std::uint32_t> level;  // the nodes of the level being joined, numbered from 1
    for (std::size_t i = 0; i < leaves; ++i)
    {
        tree.nodes.push_back({.level = 3, .leaf = random(), .children = {}});
        level.push_back(static_cast<std::uint32_t>(tree.nodes.size()));
    }
    for (std::uint8_t height = 4; level.size() > 1; ++height)
    {
        std::vector<std::uint32_t> above;
        for (std::size_t i = 0; i < level.size(); i += 4)
        {
            std::array<std::uint32_t, 4> children{};
            for (std::size_t c = 0; c < 4 && i + c < level.size(); ++c)
            {
                children.at(c) = level.at(i + c);
            }
            tree.nodes.push_back({.level = height, .leaf = 0, .children = children});
            above.push_back(static_cast<std::uint32_t>(tree.nodes.size()));
        }
        level = std::move(above);
    }
    return tree;
}

}  // namespace wxLife::test
