#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

inline constexpr Coord kMinWorldSide = 1;
/// At 100 px per cell that is 1e7 px, which still fits wx's int scrollbar positions.
inline constexpr Coord kMaxWorldSide = 100'000;

/// Why validateExtent() rejected an extent.
enum class ExtentError : std::uint8_t
{
    TooSmall,
    TooLarge,
    OverMemoryBudget
};

/// Bytes a World of this extent allocates (two padded grids). @pre 0 <= sides <= kMaxWorldSide
[[nodiscard]] constexpr std::uint64_t worldBytes(Extent e) noexcept
{
    return 2U * (static_cast<std::uint64_t>(e.width) + 2U) *
           (static_cast<std::uint64_t>(e.height) + 2U) * sizeof(Cell);
}

/// Installed RAM from sysconf() on Unix; nullopt elsewhere or when it cannot be read.
[[nodiscard]] std::optional<std::uint64_t> physicalMemoryBytes() noexcept;
/// A quarter of physical RAM clamped to [256 MiB, 16 GiB]; 2 GiB when RAM is unknown.
[[nodiscard]] std::uint64_t defaultMemoryBudget() noexcept;

/// Checks the sides first, then the memory. @return the extent itself when it can be used.
[[nodiscard]] std::expected<Extent, ExtentError> validateExtent(
    Extent e, std::uint64_t memoryBudgetBytes) noexcept;
/// Message for the user, e.g. "Needs 5.2 GiB; the limit is 4.0 GiB."
[[nodiscard]] std::string describe(ExtentError error, Extent e, std::uint64_t memoryBudgetBytes);

}  // namespace wxLife::core
