#include "wxLife/core/Pacer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

#include "wxLife/core/Speed.h"

namespace wxLife::core
{

namespace
{
using Seconds = std::chrono::duration<double>;
}

GenerationPacer::GenerationPacer(Speed speed) noexcept : speed_(speed.clamped()) { }

void GenerationPacer::setSpeed(Speed speed) noexcept
{
    speed_ = speed.clamped();
    owed_  = 0.0;
}

Speed GenerationPacer::speed() const noexcept
{
    return speed_;
}

void GenerationPacer::restart(Clock::time_point now) noexcept
{
    last_ = now;
    owed_ = 0.0;
}

TickPlan GenerationPacer::plan(Clock::time_point now) noexcept
{
    const double elapsed = Seconds(now - last_).count();

    last_ = now;
    if (speed_.unlimited)
    {
        return {.maxGenerations = std::numeric_limits<std::int64_t>::max(),
                .timeBudget     = kTickBudget};
    }

    // The cap drops debt after a stall (a modal dialog, a slow step), so no burst of catch-up steps
    // follows. It also lets the rate settle at what the machine manages when ticks run out of time.
    const double rate    = speed_.gensPerSecond;
    const double maxDebt = (Seconds(kMaxCatchUp).count() * rate) + 1.0;
    owed_                = std::clamp(owed_ + (elapsed * rate), 0.0, maxDebt);
    // Whole generations only; the fraction stays owed.
    return {.maxGenerations = static_cast<std::int64_t>(owed_), .timeBudget = kTickBudget};
}

void GenerationPacer::commit(std::int64_t generationsDone) noexcept
{
    // Unlimited ticks report more than was owed; the debt never goes negative.
    owed_ = std::max(0.0, owed_ - static_cast<double>(generationsDone));
}

void RateMeter::reset() noexcept
{
    windowStart_.reset();
    windowGenerations_ = 0;
    rate_.reset();
}

void RateMeter::record(std::int64_t generations, Clock::time_point now) noexcept
{
    // A tick that stepped nothing is not a generation boundary, so it must not open or close a
    // window.
    if (generations <= 0)
    {
        return;
    }
    if (!windowStart_)
    {
        // The first batch's steps started at an unknown time, so they are not counted.
        windowStart_ = now;
        return;
    }
    windowGenerations_ += generations;
    const Clock::duration elapsed = now - *windowStart_;
    if (elapsed < kWindow)
    {
        return;
    }
    rate_              = static_cast<double>(windowGenerations_) / Seconds(elapsed).count();
    windowStart_       = now;
    windowGenerations_ = 0;
}

std::optional<double> RateMeter::perSecond() const noexcept
{
    return rate_;
}

}  // namespace wxLife::core
