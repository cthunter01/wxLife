#include "wxLife/core/WorldLimits.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
constexpr std::uint64_t kGiB = std::uint64_t{1} << 30;

static_assert(worldBytes({.width = 0, .height = 0}) == 8);
static_assert(worldBytes({.width = 1, .height = 1}) == 18);
static_assert(worldBytes({.width = 1000, .height = 1000}) == std::uint64_t{2} * 1002 * 1002);
static_assert(worldBytes({.width = kMaxWorldSide, .height = kMaxWorldSide}) ==
              2 * std::uint64_t{100'002} * 100'002);

// The error, or "ok" when the extent is accepted.
std::string check(Extent e, std::uint64_t budget = 16 * kGiB)
{
    const auto result = validateExtent(e, budget);
    if (result)
    {
        EXPECT_EQ(*result, e);
        return "ok";
    }
    switch (result.error())
    {
        case ExtentError::TooSmall:
            return "TooSmall";
        case ExtentError::TooLarge:
            return "TooLarge";
        case ExtentError::OverMemoryBudget:
            return "OverMemoryBudget";
    }
    return "?";
}

TEST(WorldLimitsTest, SidesMustBeInRange)
{
    EXPECT_EQ(check({0, 10}), "TooSmall");
    EXPECT_EQ(check({10, 0}), "TooSmall");
    EXPECT_EQ(check({-5, 10}), "TooSmall");
    EXPECT_EQ(check({100'001, 10}), "TooLarge");
    EXPECT_EQ(check({10, 100'001}), "TooLarge");
    EXPECT_EQ(check({0, 100'001}), "TooSmall");  // sides are checked in that order
    EXPECT_EQ(check({1, 1}), "ok");
    EXPECT_EQ(check({100, 1}), "ok");
    EXPECT_EQ(check({1, 100}), "ok");
    EXPECT_EQ(check({kMaxWorldSide, 1}), "ok");
}

TEST(WorldLimitsTest, MemoryBudget)
{
    EXPECT_EQ(check({50'000, 50'000}, 4 * kGiB), "OverMemoryBudget");  // needs about 4.7 GiB
    EXPECT_EQ(check({20'000, 20'000}, 4 * kGiB), "ok");
    EXPECT_EQ(check({100'001, 100'001}, 0), "TooLarge");  // sides are checked first

    const Extent e{.width = 1234, .height = 567};
    EXPECT_EQ(check(e, worldBytes(e)), "ok");  // exactly at the limit
    EXPECT_EQ(check(e, worldBytes(e) - 1), "OverMemoryBudget");
}

TEST(WorldLimitsTest, DefaultBudgetIsAQuarterOfRamWithinLimits)
{
    const std::uint64_t budget = defaultMemoryBudget();
    EXPECT_GE(budget, 256 * kMiB);
    EXPECT_LE(budget, 16 * kGiB);
    if (const auto ram = physicalMemoryBytes())
    {
        EXPECT_GT(*ram, 0U);
        EXPECT_EQ(budget, std::clamp(*ram / 4, 256 * kMiB, 16 * kGiB));
    }
    else
    {
        EXPECT_EQ(budget, 2 * kGiB);
    }
}

TEST(WorldLimitsTest, Describe)
{
    EXPECT_EQ(describe(ExtentError::TooSmall, {0, 5}, 4 * kGiB),
              "Width and height must be at least 1.");
    EXPECT_EQ(describe(ExtentError::TooLarge, {200'000, 5}, 4 * kGiB),
              "Width and height must be at most 100,000.");
    EXPECT_EQ(describe(ExtentError::OverMemoryBudget, {50'000, 50'000}, 4 * kGiB),
              "Needs 4.7 GiB; the limit is 4.0 GiB.");
    EXPECT_EQ(describe(ExtentError::OverMemoryBudget, {1000, 1000}, 256 * kMiB / 1024),
              "Needs 1.9 MiB; the limit is 256 KiB.");
}

}  // namespace
}  // namespace wxLife::core
