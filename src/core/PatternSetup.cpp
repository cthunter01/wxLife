#include "wxLife/core/PatternSetup.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>

#include "wxLife/core/Demo.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{

namespace
{

// The top-left corner that centres `pattern` in `world`.
[[nodiscard]] constexpr CellPos centred(Extent pattern, Extent world) noexcept
{
    return {.x = (world.width - pattern.width) / 2, .y = (world.height - pattern.height) / 2};
}

}  // namespace

PatternSetup demoSetup(const Demo& demo, const Pattern& pattern)
{
    const bool    unbounded = demo.kind == WorldKind::UNBOUNDED;
    const CellPos origin    = demo.origin.value_or(
        unbounded ? CellPos{.x = -(pattern.extent.width / 2), .y = -(pattern.extent.height / 2)}
                  : centred(pattern.extent, demo.world));
    PatternSetup setup{
        .kind         = demo.kind,
        .world        = unbounded ? Extent{} : demo.world,
        .origin       = origin,
        .topology     = demo.topology,
        .rule         = pattern.rule.value_or(Rule{}),
        .automaton    = demo.automaton,
        .ants         = {demo.ants.begin(), demo.ants.end()},
        .speed        = demo.speed,
        .stepExponent = unbounded ? std::optional(demo.stepExponent) : std::nullopt,
        .view         = std::nullopt,
    };
    if (demo.view)
    {
        // The pattern's top-left corner: a macrocell's is where its file puts its cells.
        const UniversePos corner =
            pattern.tree ? UniversePos{.x = pattern.tree->bounds.x0, .y = pattern.tree->bounds.y0}
                         : UniversePos{.x = origin.x, .y = origin.y};
        setup.view = UniverseRect{.x0 = corner.x + demo.view->x0,
                                  .y0 = corner.y + demo.view->y0,
                                  .x1 = corner.x + demo.view->x1,
                                  .y1 = corner.y + demo.view->y1};
    }
    return setup;
}

std::expected<PatternSetup, ExtentError> fileSetup(const Pattern& pattern, const Rule& currentRule,
                                                   WorldKind currentKind, Topology currentTopology,
                                                   std::uint64_t memoryBudgetBytes)
{
    if (pattern.tree)
    {
        return PatternSetup{
            .kind         = WorldKind::UNBOUNDED,
            .world        = {},
            .origin       = {},
            .topology     = currentTopology,
            .rule         = pattern.rule.value_or(currentRule),
            .automaton    = Automaton::LIFE,
            .ants         = {},
            .speed        = std::nullopt,
            .stepExponent = std::nullopt,
            .view         = std::nullopt,
        };
    }
    const Extent size = pattern.extent;
    if (currentKind == WorldKind::UNBOUNDED)
    {
        return PatternSetup{
            .kind         = WorldKind::UNBOUNDED,
            .world        = {},
            .origin       = {.x = -(size.width / 2), .y = -(size.height / 2)},
            .topology     = currentTopology,
            .rule         = pattern.rule.value_or(currentRule),
            .automaton    = Automaton::LIFE,
            .ants         = {},
            .speed        = std::nullopt,
            .stepExponent = std::nullopt,
            .view         = std::nullopt,
        };
    }
    const auto side = [](Coord length, Coord margin) {
        return static_cast<Coord>(std::clamp<std::int64_t>(
            std::int64_t{length} + (2 * std::int64_t{margin}), kMinWorldSide, kMaxWorldSide));
    };
    // Room on each side, halved until the world fits the budget, down to none at all.
    Coord  marginX = std::max(kMinFileMargin, size.width / 2);
    Coord  marginY = std::max(kMinFileMargin, size.height / 2);
    Extent world{.width = side(size.width, marginX), .height = side(size.height, marginY)};
    std::expected<Extent, ExtentError> valid = validateExtent(world, memoryBudgetBytes);
    while (!valid && (marginX > 0 || marginY > 0))
    {
        marginX /= 2;
        marginY /= 2;
        world = {.width = side(size.width, marginX), .height = side(size.height, marginY)};
        valid = validateExtent(world, memoryBudgetBytes);
    }
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    return PatternSetup{
        .kind         = WorldKind::FIXED_SIZE,
        .world        = world,
        .origin       = centred(size, world),
        .topology     = currentTopology,
        .rule         = pattern.rule.value_or(currentRule),
        .automaton    = Automaton::LIFE,
        .ants         = {},
        .speed        = std::nullopt,
        .stepExponent = std::nullopt,
        .view         = std::nullopt,
    };
}

}  // namespace wxLife::core
