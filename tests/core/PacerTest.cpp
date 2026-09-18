#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Pacer.h"
#include "wxLife/core/Speed.h"

namespace wxLife::core
{
namespace
{

using namespace std::chrono_literals;

// Synthetic time: nothing here sleeps or reads the clock.
constexpr Clock::time_point kStart = Clock::time_point{} + 1h;

// Restarts the pacer, then ticks every `interval` for `duration`, stepping everything each plan
// allows. Returns the generations stepped.
std::int64_t runTicks(GenerationPacer& pacer, Clock::duration interval, Clock::duration duration)
{
    std::int64_t total = 0;
    pacer.restart(kStart);
    for (Clock::time_point now = kStart + interval; now <= kStart + duration; now += interval)
    {
        const TickPlan plan = pacer.plan(now);
        pacer.commit(plan.maxGenerations);
        total += plan.maxGenerations;
    }
    return total;
}

TEST(PacerTest, TenPerSecondOverOneSecondOfFrames)
{
    GenerationPacer    pacer(Speed{.gensPerSecond = 10, .unlimited = false});
    const std::int64_t total = runTicks(pacer, 16ms, 1s);
    EXPECT_GE(total, 9);
    EXPECT_LE(total, 11);
}

TEST(PacerTest, FastSpeedsStepSeveralGenerationsPerTick)
{
    GenerationPacer pacer(Speed{.gensPerSecond = 250, .unlimited = false});
    pacer.restart(kStart);
    std::int64_t total = 0;
    for (int tick = 1; tick <= 62; ++tick)
    {
        const TickPlan plan = pacer.plan(kStart + tick * 16ms);
        EXPECT_GE(plan.maxGenerations, 3) << "tick " << tick;  // 16 ms at 250 gen/s = 4 generations
        EXPECT_LE(plan.maxGenerations, 5) << "tick " << tick;
        EXPECT_EQ(plan.timeBudget, GenerationPacer::kTickBudget);
        pacer.commit(plan.maxGenerations);
        total += plan.maxGenerations;
    }
    EXPECT_GE(total, 247);  // 62 ticks = 0.992 s = 248 generations
    EXPECT_LE(total, 249);

    const std::int64_t tenSeconds = runTicks(pacer, 16ms, 10s);
    EXPECT_GE(tenSeconds, 2499);
    EXPECT_LE(tenSeconds, 2501);
}

TEST(PacerTest, NothingIsDueBeforeTheFirstGenerationsWorthOfTime)
{
    GenerationPacer pacer(Speed{.gensPerSecond = 10, .unlimited = false});
    pacer.restart(kStart);
    EXPECT_EQ(pacer.plan(kStart + 50ms).maxGenerations, 0);
    EXPECT_EQ(pacer.plan(kStart + 99ms).maxGenerations, 0);
    EXPECT_EQ(pacer.plan(kStart + 101ms).maxGenerations, 1);
}

TEST(PacerTest, AStallIsCappedAtTheCatchUpLimit)
{
    GenerationPacer pacer(Speed{.gensPerSecond = 100, .unlimited = false});
    pacer.restart(kStart);
    const TickPlan plan = pacer.plan(kStart + 5s);
    // kMaxCatchUp (250 ms) at 100 gen/s, plus the one generation the cap allows on top.
    EXPECT_EQ(plan.maxGenerations, 26);
}

TEST(PacerTest, UnpaidDebtStaysCapped)
{
    // Ticks that run out of time pay nothing back; the debt must not grow without bound.
    GenerationPacer pacer(Speed{.gensPerSecond = 1000, .unlimited = false});
    pacer.restart(kStart);
    for (int tick = 1; tick <= 100; ++tick)
    {
        EXPECT_LE(pacer.plan(kStart + tick * 16ms).maxGenerations, 251);
    }
    pacer.commit(251);
    EXPECT_LE(pacer.plan(kStart + 100 * 16ms).maxGenerations, 1);
}

TEST(PacerTest, SetSpeedAndRestartClearTheDebt)
{
    GenerationPacer pacer(Speed{.gensPerSecond = 10, .unlimited = false});
    pacer.restart(kStart);
    ASSERT_EQ(pacer.plan(kStart + 1s).maxGenerations, 3);  // capped: 0.25 s × 10 + 1 = 3.5
    pacer.setSpeed(Speed{.gensPerSecond = 10, .unlimited = false});
    EXPECT_EQ(pacer.plan(kStart + 1s).maxGenerations, 0);

    ASSERT_EQ(pacer.plan(kStart + 2s).maxGenerations, 3);
    pacer.restart(kStart + 2s);
    EXPECT_EQ(pacer.plan(kStart + 2s).maxGenerations, 0);
}

TEST(PacerTest, UnlimitedAllowsEverythingWithinTheBudget)
{
    GenerationPacer pacer(Speed{.gensPerSecond = 30, .unlimited = true});
    pacer.restart(kStart);
    const TickPlan plan = pacer.plan(kStart + 16ms);
    EXPECT_EQ(plan.maxGenerations, std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(plan.timeBudget, GenerationPacer::kTickBudget);
    pacer.commit(1234);  // whatever fitted in the budget

    // Back to a finite speed: no debt is left over from the unlimited ticks.
    pacer.setSpeed(Speed{.gensPerSecond = 30, .unlimited = false});
    EXPECT_EQ(pacer.plan(kStart + 32ms).maxGenerations, 0);
}

TEST(PacerTest, KeepsTheSpeedClamped)
{
    EXPECT_EQ(GenerationPacer{}.speed(), Speed{});
    EXPECT_EQ(GenerationPacer(Speed{5000, false}).speed(), (Speed{1000, false}));
    GenerationPacer pacer;
    pacer.setSpeed(Speed{.gensPerSecond = 0, .unlimited = true});
    EXPECT_EQ(pacer.speed(), (Speed{1, true}));
}

TEST(RateMeterTest, MeasuresBetweenBatchesThatStepped)
{
    RateMeter meter;
    meter.reset();
    EXPECT_FALSE(meter.perSecond());

    meter.record(0, kStart + 50ms);    // nothing stepped: ignored
    meter.record(10, kStart + 100ms);  // opens the window; its own steps are not counted
    meter.record(20, kStart + 400ms);
    meter.record(0, kStart + 700ms);  // would close the window if it counted
    EXPECT_FALSE(meter.perSecond());
    meter.record(10, kStart + 1100ms);
    EXPECT_DOUBLE_EQ(meter.perSecond().value(), 30.0);  // 30 generations in 1 s

    meter.record(100, kStart + 1200ms);
    EXPECT_DOUBLE_EQ(meter.perSecond().value(), 30.0);  // unchanged until the window closes
    meter.record(50, kStart + 1100ms + RateMeter::kWindow);
    EXPECT_DOUBLE_EQ(meter.perSecond().value(), 300.0);  // 150 generations in 0.5 s

    meter.reset();
    EXPECT_FALSE(meter.perSecond());
    meter.record(5, kStart + 11s);
    EXPECT_FALSE(meter.perSecond());
}

// Every rate a meter reports while a runner steps what the pacer allows, every 16 ms for 10 s.
std::vector<double> measuredRates(Speed speed)
{
    GenerationPacer pacer(speed);
    RateMeter       meter;
    pacer.restart(kStart);
    meter.reset();
    std::vector<double> rates;
    for (Clock::time_point now = kStart + 16ms; now <= kStart + 10s; now += 16ms)
    {
        const std::int64_t generations = pacer.plan(now).maxGenerations;
        pacer.commit(generations);
        meter.record(generations, now);
        if (const std::optional<double> rate = meter.perSecond())
        {
            rates.push_back(*rate);
        }
    }
    return rates;
}

// Windows that closed on ticks without a step used to hold k or k + 1 generations, so a slow target
// showed as 0 and 2 alternately at 1 gen/s, and as 3.9 and 5.9 at 5 gen/s.
TEST(RateMeterTest, OneGenerationPerSecondMeasuresAsOne)
{
    const std::vector<double> rates = measuredRates(Speed{.gensPerSecond = 1, .unlimited = false});
    ASSERT_FALSE(rates.empty());
    for (const double rate : rates)
    {
        EXPECT_NEAR(rate, 1.0, 0.02);
    }
}

TEST(RateMeterTest, FiveGenerationsPerSecondMeasuresAsFive)
{
    const std::vector<double> rates = measuredRates(Speed{.gensPerSecond = 5, .unlimited = false});
    ASSERT_FALSE(rates.empty());
    for (const double rate : rates)
    {
        EXPECT_NEAR(rate, 5.0, 0.1);
    }
}

}  // namespace
}  // namespace wxLife::core
