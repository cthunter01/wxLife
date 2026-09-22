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

GenerationPacer::GenerationPacer(Speed speed) noexcept : m_speed(speed.clamped()) { }

void GenerationPacer::setSpeed(Speed speed) noexcept
{
    m_speed = speed.clamped();
    m_owed  = 0.0;
}

Speed GenerationPacer::speed() const noexcept
{
    return m_speed;
}

void GenerationPacer::restart(Clock::time_point now) noexcept
{
    m_last = now;
    m_owed = 0.0;
}

TickPlan GenerationPacer::plan(Clock::time_point now) noexcept
{
    const double elapsed = Seconds(now - m_last).count();

    m_last = now;
    if (m_speed.unlimited)
    {
        return {.maxGenerations = std::numeric_limits<std::int64_t>::max(),
                .timeBudget     = kTickBudget};
    }

    // The cap drops debt after a stall (a modal dialog, a slow step), so no burst of catch-up steps
    // follows. It also lets the rate settle at what the machine manages when ticks run out of time.
    const double rate    = m_speed.gensPerSecond;
    const double maxDebt = (Seconds(kMaxCatchUp).count() * rate) + 1.0;
    m_owed               = std::clamp(m_owed + (elapsed * rate), 0.0, maxDebt);
    // Whole generations only; the fraction stays owed.
    return {.maxGenerations = static_cast<std::int64_t>(m_owed), .timeBudget = kTickBudget};
}

void GenerationPacer::commit(std::int64_t generationsDone) noexcept
{
    // Unlimited ticks report more than was owed; the debt never goes negative.
    m_owed = std::max(0.0, m_owed - static_cast<double>(generationsDone));
}

void RateMeter::reset() noexcept
{
    m_windowStart.reset();
    m_windowGenerations = 0;
    m_rate.reset();
}

void RateMeter::record(std::int64_t generations, Clock::time_point now) noexcept
{
    // A tick that stepped nothing is not a generation boundary, so it must not open or close a
    // window.
    if (generations <= 0)
    {
        return;
    }
    if (!m_windowStart)
    {
        // The first batch's steps started at an unknown time, so they are not counted.
        m_windowStart = now;
        return;
    }
    m_windowGenerations += generations;
    const Clock::duration elapsed = now - *m_windowStart;
    if (elapsed < kWindow)
    {
        return;
    }
    m_rate              = static_cast<double>(m_windowGenerations) / Seconds(elapsed).count();
    m_windowStart       = now;
    m_windowGenerations = 0;
}

std::optional<double> RateMeter::perSecond() const noexcept
{
    return m_rate;
}

}  // namespace wxLife::core
