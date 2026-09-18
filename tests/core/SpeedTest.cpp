#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include <gtest/gtest.h>

#include "wxLife/core/Speed.h"

namespace wxLife::core
{
namespace
{

// faster() and slower() are constexpr, so the whole walk is checked at compile time.
static_assert(Speed{}.faster() == Speed{.gensPerSecond = 60, .unlimited = false});
static_assert(Speed{}.slower() == Speed{.gensPerSecond = 20, .unlimited = false});
static_assert(Speed{.gensPerSecond = 40, .unlimited = false}.faster() ==
              Speed{.gensPerSecond = 60, .unlimited = false});  // between two steps
static_assert(Speed{.gensPerSecond = 40, .unlimited = false}.slower() ==
              Speed{.gensPerSecond = 30, .unlimited = false});
static_assert(Speed{.gensPerSecond = 1000, .unlimited = false}.faster() ==
              Speed{.gensPerSecond = 1000, .unlimited = true});  // past the last step: Max
static_assert(Speed{.gensPerSecond = 1000, .unlimited = true}.faster() ==
              Speed{.gensPerSecond = 1000, .unlimited = true});
static_assert(Speed{.gensPerSecond = 30, .unlimited = true}.slower() ==
              Speed{.gensPerSecond = 30, .unlimited = false});  // leaving Max restores the rate
static_assert(Speed{.gensPerSecond = 1, .unlimited = false}.slower() ==
              Speed{.gensPerSecond = 1, .unlimited = false});
static_assert(Speed{.gensPerSecond = 0, .unlimited = false}.faster() ==
              Speed{.gensPerSecond = 2, .unlimited = false});  // clamped to 1 first

constexpr bool walksEveryStepUpAndDown()
{
    Speed speed{.gensPerSecond = Speed::kSteps.front(), .unlimited = false};
    for (std::size_t i = 1; i < Speed::kSteps.size(); ++i)
    {
        speed = speed.faster();
        if (speed != Speed{.gensPerSecond = Speed::kSteps.at(i), .unlimited = false})
        {
            return false;
        }
    }
    speed = speed.faster();
    if (speed != Speed{.gensPerSecond = Speed::kMax, .unlimited = true})
    {
        return false;
    }
    speed = speed.slower();
    if (speed != Speed{.gensPerSecond = Speed::kMax, .unlimited = false})
    {
        return false;
    }
    for (std::size_t i = Speed::kSteps.size() - 1; i-- > 0;)
    {
        speed = speed.slower();
        if (speed != Speed{.gensPerSecond = Speed::kSteps.at(i), .unlimited = false})
        {
            return false;
        }
    }
    return true;
}
static_assert(walksEveryStepUpAndDown());

static_assert(Speed{.gensPerSecond = 0, .unlimited = false}.clamped() ==
              Speed{.gensPerSecond = 1, .unlimited = false});
static_assert(Speed{.gensPerSecond = -7, .unlimited = true}.clamped() ==
              Speed{.gensPerSecond = 1, .unlimited = true});
static_assert(Speed{.gensPerSecond = 5000, .unlimited = false}.clamped() ==
              Speed{.gensPerSecond = 1000, .unlimited = false});
static_assert(Speed{.gensPerSecond = 250, .unlimited = true}.clamped() ==
              Speed{.gensPerSecond = 250, .unlimited = true});

TEST(SpeedTest, DefaultIsThirtyGenerationsPerSecond)
{
    EXPECT_EQ(Speed{}.gensPerSecond, 30);
    EXPECT_FALSE(Speed{}.unlimited);
}

TEST(SpeedTest, SliderEnds)
{
    EXPECT_EQ(Speed::fromSliderPosition(0), 1);
    EXPECT_EQ(Speed::fromSliderPosition(100), 10);
    EXPECT_EQ(Speed::fromSliderPosition(200), 100);
    EXPECT_EQ(Speed::fromSliderPosition(Speed::kSliderMax), 1000);
    EXPECT_EQ(Speed::fromSliderPosition(-50), 1);  // clamped
    EXPECT_EQ(Speed::fromSliderPosition(999), 1000);

    EXPECT_EQ(Speed::toSliderPosition(1), 0);
    EXPECT_EQ(Speed::toSliderPosition(30), 148);
    EXPECT_EQ(Speed::toSliderPosition(1000), Speed::kSliderMax);
    EXPECT_EQ(Speed::toSliderPosition(0), 0);
    EXPECT_EQ(Speed::toSliderPosition(5000), Speed::kSliderMax);
}

TEST(SpeedTest, SliderIsMonotonic)
{
    for (int p = 1; p <= Speed::kSliderMax; ++p)
    {
        EXPECT_LE(Speed::fromSliderPosition(p - 1), Speed::fromSliderPosition(p))
            << "position " << p;
    }
    for (int rate = Speed::kMin + 1; rate <= Speed::kMax; ++rate)
    {
        EXPECT_LE(Speed::toSliderPosition(rate - 1), Speed::toSliderPosition(rate))
            << "rate " << rate;
    }
}

TEST(SpeedTest, SliderRoundTripsWithinOnePosition)
{
    // From a rate: the chosen position is the nearest, so the rates one position either side
    // bracket it.
    for (int rate = Speed::kMin; rate <= Speed::kMax; ++rate)
    {
        const int p = Speed::toSliderPosition(rate);
        EXPECT_LE(Speed::fromSliderPosition(std::max(p - 1, 0)), rate) << "rate " << rate;
        EXPECT_GE(Speed::fromSliderPosition(std::min(p + 1, Speed::kSliderMax)), rate)
            << "rate " << rate;
    }
    // From a position: several low positions share a rate, so the way back may land on another
    // position with that same rate; otherwise it lands at most one position away.
    for (int p = 0; p <= Speed::kSliderMax; ++p)
    {
        const int rate = Speed::fromSliderPosition(p);
        const int back = Speed::toSliderPosition(rate);
        EXPECT_TRUE(std::abs(back - p) <= 1 || Speed::fromSliderPosition(back) == rate)
            << "position " << p;
    }
}

TEST(SpeedTest, ToString)
{
    EXPECT_EQ(toString(Speed{}), "30 gen/s");
    EXPECT_EQ(toString(Speed{1, false}), "1 gen/s");
    EXPECT_EQ(toString(Speed{1000, false}), "1000 gen/s");
    EXPECT_EQ(toString(Speed{30, true}), "Max");
}

}  // namespace
}  // namespace wxLife::core
