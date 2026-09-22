#include "wxLife/core/Format.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace wxLife::core
{
namespace
{

constexpr std::uint64_t kKiB = 1024;
constexpr std::uint64_t kMiB = kKiB * 1024;
constexpr std::uint64_t kGiB = kMiB * 1024;
constexpr std::uint64_t kTiB = kGiB * 1024;
constexpr std::uint64_t kEiB = kTiB * 1024 * 1024;

TEST(FormatTest, CountGroupsThousands)
{
    EXPECT_EQ(formatCount(0), "0");
    EXPECT_EQ(formatCount(7), "7");
    EXPECT_EQ(formatCount(999), "999");
    EXPECT_EQ(formatCount(1000), "1,000");
    EXPECT_EQ(formatCount(12'345), "12,345");
    EXPECT_EQ(formatCount(100'000), "100,000");
    EXPECT_EQ(formatCount(1'234'567), "1,234,567");
    EXPECT_EQ(formatCount(std::numeric_limits<std::uint64_t>::max()), "18,446,744,073,709,551,615");
}

TEST(FormatTest, BytesBelowOneKibibyte)
{
    EXPECT_EQ(formatBytes(0), "0 B");
    EXPECT_EQ(formatBytes(812), "812 B");
    EXPECT_EQ(formatBytes(1023), "1023 B");
}

TEST(FormatTest, BytesUseOneDecimalBelowOneHundred)
{
    EXPECT_EQ(formatBytes(kKiB), "1.0 KiB");
    EXPECT_EQ(formatBytes(1536), "1.5 KiB");
    EXPECT_EQ(formatBytes(std::uint64_t{2} * 1002 * 1002), "1.9 MiB");  // a 1000² world
    EXPECT_EQ(formatBytes(4 * kGiB), "4.0 GiB");
    EXPECT_EQ(formatBytes((15 * kGiB) + (600 * kMiB)), "15.6 GiB");
    EXPECT_EQ(formatBytes(102'348), "99.9 KiB");
}

TEST(FormatTest, BytesDropTheDecimalFromOneHundred)
{
    EXPECT_EQ(formatBytes(102'349), "100 KiB");                       // 99.95 KiB rounds up
    EXPECT_EQ(formatBytes(std::uint64_t{2} * 514 * 514), "516 KiB");  // a 512² world
    EXPECT_EQ(formatBytes(1023 * kMiB), "1023 MiB");
}

TEST(FormatTest, BytesMoveToTheNextUnitAtEachBoundary)
{
    EXPECT_EQ(formatBytes(kMiB - 1), "1.0 MiB");  // would otherwise read "1024 KiB"
    EXPECT_EQ(formatBytes(kMiB - 513), "1023 KiB");
    EXPECT_EQ(formatBytes(kMiB), "1.0 MiB");
    EXPECT_EQ(formatBytes(kGiB - 1), "1.0 GiB");
    EXPECT_EQ(formatBytes(kGiB), "1.0 GiB");
    EXPECT_EQ(formatBytes(kTiB), "1.0 TiB");
    EXPECT_EQ(formatBytes(kTiB * 1024), "1.0 PiB");
    EXPECT_EQ(formatBytes(kEiB), "1.0 EiB");
    EXPECT_EQ(formatBytes(std::numeric_limits<std::uint64_t>::max()), "16.0 EiB");
}

}  // namespace
}  // namespace wxLife::core
