#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

#include <gtest/gtest.h>

#include "wxLife/core/Random.h"

namespace wxLife::core
{
namespace
{

static_assert(std::uniform_random_bit_generator<SplitMix64>);

constexpr bool startsWithPublishedSequence()
{
    SplitMix64          random(0);
    const std::uint64_t first  = random();
    const std::uint64_t second = random();
    return first == 0xE220A8397B1DCDAFU && second == 0x6E789E6AA1B965F4U;
}
static_assert(startsWithPublishedSequence());

TEST(RandomTest, SeedZeroGivesThePublishedSequence)
{
    // First outputs of the reference splitmix64.c with x = 0.
    constexpr std::array<std::uint64_t, 5> kExpected{0xE220A8397B1DCDAFU, 0x6E789E6AA1B965F4U,
                                                     0x06C45D188009454FU, 0xF88BB8A8724C81ECU,
                                                     0x1B39896A51A8749BU};
    SplitMix64                             random(0);
    for (const std::uint64_t expected : kExpected)
    {
        EXPECT_EQ(random(), expected);
    }
}

TEST(RandomTest, SameSeedSameSequenceOtherSeedOtherSequence)
{
    SplitMix64 a(12345);
    SplitMix64 b(12345);
    SplitMix64 c(12346);
    for (int i = 0; i < 100; ++i)
    {
        const std::uint64_t value = a();
        EXPECT_EQ(b(), value);
        EXPECT_NE(c(), value);
    }
}

TEST(RandomTest, WorksWithStandardDistributions)
{
    SplitMix64                         random(1);
    std::uniform_int_distribution<int> die(1, 6);
    std::array<int, 7>                 counts{};
    for (int i = 0; i < 6000; ++i)
    {
        ++counts.at(static_cast<std::size_t>(die(random)));
    }
    EXPECT_EQ(counts[0], 0);
    for (std::size_t face = 1; face <= 6; ++face)
    {
        EXPECT_NEAR(counts.at(face), 1000, 150) << "face " << face;
    }
}

}  // namespace
}  // namespace wxLife::core
