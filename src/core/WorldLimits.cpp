#include "wxLife/core/WorldLimits.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "wxLife/core/Format.h"
#include "wxLife/core/Types.h"

// The physical memory size is the one OS query in core.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // NOLINT(cppcoreguidelines-macro-usage): <windows.h> reads it
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // NOLINT(cppcoreguidelines-macro-usage): <windows.h> reads it
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

namespace wxLife::core
{

namespace
{
constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
constexpr std::uint64_t kGiB = std::uint64_t{1} << 30;
}  // namespace

std::optional<std::uint64_t> physicalMemoryBytes() noexcept
{
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = static_cast<DWORD>(sizeof(status));
    if (GlobalMemoryStatusEx(&status) != 0)
    {
        return std::uint64_t{status.ullTotalPhys};
    }
#elif defined(__APPLE__) || defined(__linux__)
    const long pages    = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
    {
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
    }
#endif
    return std::nullopt;  // another OS, or the query failed
}

std::uint64_t defaultMemoryBudget() noexcept
{
    const std::optional<std::uint64_t> ram = physicalMemoryBytes();
    if (!ram)
    {
        return 2 * kGiB;
    }
    return std::clamp(*ram / 4, 256 * kMiB, 16 * kGiB);
}

std::expected<Extent, ExtentError> validateExtent(Extent        e,
                                                  std::uint64_t memoryBudgetBytes) noexcept
{
    if (e.width < kMinWorldSide || e.height < kMinWorldSide)
    {
        return std::unexpected(ExtentError::TooSmall);
    }
    if (e.width > kMaxWorldSide || e.height > kMaxWorldSide)
    {
        return std::unexpected(ExtentError::TooLarge);
    }
    if (worldBytes(e) > memoryBudgetBytes)
    {
        return std::unexpected(ExtentError::OverMemoryBudget);
    }
    return e;
}

std::string describe(ExtentError error, Extent e, std::uint64_t memoryBudgetBytes)
{
    switch (error)
    {
        case ExtentError::TooSmall:
            return std::format("Width and height must be at least {}.", formatCount(kMinWorldSide));
        case ExtentError::TooLarge:
            return std::format("Width and height must be at most {}.", formatCount(kMaxWorldSide));
        case ExtentError::OverMemoryBudget:
            return std::format("Needs {}; the limit is {}.", formatBytes(worldBytes(e)),
                               formatBytes(memoryBudgetBytes));
    }
    std::unreachable();
}

}  // namespace wxLife::core
