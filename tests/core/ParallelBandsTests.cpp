#include "wxLife/core/ParallelBands.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

static_assert(bandStart(1000, 3, 0) == 0);
static_assert(bandStart(1000, 3, 1) == 333);
static_assert(bandStart(1000, 3, 3) == 1000);
static_assert(bandStart(7, 2, 1) == 3);

struct BandCase
{
    Coord    rows;
    unsigned bands;
};

class ForEachBandTest : public testing::TestWithParam<BandCase>
{ };

TEST_P(ForEachBandTest, CoversEveryRowOnceWithContiguousBands)
{
    const auto [rows, bands]     = GetParam();
    const unsigned expectedBands = std::clamp(bands, 1U, static_cast<unsigned>(std::max(rows, 1)));

    // Each band writes only its own slots, so the job needs no locks.
    std::vector<std::pair<Coord, Coord>> ranges(bands, {-1, -1});
    std::vector<int>                     calls(bands, 0);
    std::vector<int>                     timesVisited(static_cast<std::size_t>(rows), 0);
    forEachBand(rows, bands, [&](unsigned band, Coord firstRow, Coord endRow) {
        ranges[band] = {firstRow, endRow};
        ++calls[band];
        for (Coord y = firstRow; y < endRow; ++y)
        {
            ++timesVisited[static_cast<std::size_t>(y)];
        }
    });

    EXPECT_TRUE(std::ranges::all_of(timesVisited, [](int n) { return n == 1; }));
    Coord next = 0;
    for (unsigned band = 0; band < bands; ++band)
    {
        const bool used = band < expectedBands;
        EXPECT_EQ(calls[band], used ? 1 : 0) << "band " << band;
        if (!used)
        {
            continue;
        }
        EXPECT_EQ(ranges[band].first, next) << "band " << band;
        EXPECT_LE(ranges[band].first, ranges[band].second) << "band " << band;
        next = ranges[band].second;
    }
    EXPECT_EQ(next, rows);
}

std::vector<BandCase> allBandCases()
{
    std::vector<BandCase> cases;
    for (const Coord rows : {0, 1, 7, 1000})
    {
        for (const unsigned bands : {1U, 2U, 3U, 64U})
        {
            cases.push_back({.rows = rows, .bands = bands});
        }
    }
    return cases;
}

INSTANTIATE_TEST_SUITE_P(RowsAndBands, ForEachBandTest, testing::ValuesIn(allBandCases()),
                         [](const testing::TestParamInfo<BandCase>& test) {
                             return std::format("{}_rows_{}_bands", test.param.rows,
                                                test.param.bands);
                         });

TEST(ParallelBandsTest, ZeroBandsMeansOne)
{
    int calls = 0;
    forEachBand(10, 0, [&](unsigned band, Coord firstRow, Coord endRow) {
        ++calls;
        EXPECT_EQ(band, 0U);
        EXPECT_EQ(firstRow, 0);
        EXPECT_EQ(endRow, 10);
    });
    EXPECT_EQ(calls, 1);
}

TEST(ParallelBandsTest, BandZeroRunsOnTheCallingThread)
{
    std::vector<std::thread::id> threads(4);
    forEachBand(100, 4,
                [&](unsigned band, Coord, Coord) { threads[band] = std::this_thread::get_id(); });
    EXPECT_EQ(threads[0], std::this_thread::get_id());
    for (std::size_t band = 1; band < threads.size(); ++band)
    {
        EXPECT_NE(threads[band], std::this_thread::get_id());
    }
}

TEST(ParallelBandsTest, SuggestedBandCountOneBandPerMinimumChunk)
{
    EXPECT_EQ(suggestedBandCount(0, 8), 1U);
    EXPECT_EQ(suggestedBandCount(-5, 8), 1U);
    EXPECT_EQ(suggestedBandCount(kMinCellsPerBand - 1, 8), 1U);
    EXPECT_EQ(suggestedBandCount(4 * kMinCellsPerBand, 8), 4U);
    EXPECT_EQ(suggestedBandCount((5 * kMinCellsPerBand) - 1, 8), 4U);
    EXPECT_EQ(suggestedBandCount(100 * kMinCellsPerBand, 8), 8U);
    EXPECT_EQ(suggestedBandCount(100 * kMinCellsPerBand, 1), 1U);
}

TEST(ParallelBandsTest, SuggestedBandCountSplitsOnlyFromFourBandsOfWork)
{
    EXPECT_EQ(suggestedBandCount(2 * kMinCellsPerBand, 8), 1U);
    EXPECT_EQ(suggestedBandCount((4 * kMinCellsPerBand) - 1, 8), 1U);
    // The thread cap applies afterwards, so a caller can still ask for two or three.
    EXPECT_EQ(suggestedBandCount(100 * kMinCellsPerBand, 2), 2U);
    EXPECT_EQ(suggestedBandCount(100 * kMinCellsPerBand, 3), 3U);
}

TEST(ParallelBandsTest, SuggestedBandCountCustomMinimum)
{
    EXPECT_EQ(suggestedBandCount(3, 64, 1), 1U);
    EXPECT_EQ(suggestedBandCount(5, 64, 1), 5U);
    EXPECT_EQ(suggestedBandCount(1000, 64, 1), 64U);
    EXPECT_EQ(suggestedBandCount(1000, 2, 1), 2U);
    EXPECT_EQ(suggestedBandCount(1000, 64, 0), 64U);   // treated as 1
    EXPECT_EQ(suggestedBandCount(1000, 64, -3), 64U);  // treated as 1
    EXPECT_EQ(suggestedBandCount(1000, 64, 100), 10U);
    EXPECT_EQ(suggestedBandCount(399, 64, 100), 1U);
}

TEST(ParallelBandsTest, SuggestedBandCountZeroThreadsMeansHardwareConcurrency)
{
    const unsigned hardware = std::max(std::thread::hardware_concurrency(), 1U);
    EXPECT_EQ(suggestedBandCount(1'000'000'000'000, 0), hardware);
    EXPECT_EQ(suggestedBandCount(4 * kMinCellsPerBand, 0), std::min(hardware, 4U));
    EXPECT_EQ(suggestedBandCount(kMinCellsPerBand, 0), 1U);
}

}  // namespace
}  // namespace wxLife::core
