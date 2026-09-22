#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "wxLife/core/Speed.h"

namespace wxLife::core
{

using Clock = std::chrono::steady_clock;

/// What one timer tick may do.
struct TickPlan
{
    std::int64_t maxGenerations = 0;  ///< 0 = nothing due yet; INT64_MAX when unlimited.
    /// Stop once this much time was spent; when maxGenerations > 0, the first step always runs.
    Clock::duration timeBudget{};
};

/// Turns elapsed wall time into generations owed at the current Speed.
/// Time is passed in, so tests never sleep.
class GenerationPacer
{
public:
    static constexpr std::chrono::milliseconds kTickBudget{10};
    static constexpr std::chrono::milliseconds kMaxCatchUp{250};  ///< Debt beyond this is dropped.

    GenerationPacer() = default;
    explicit GenerationPacer(Speed speed) noexcept;

    /// Also drops accumulated debt.
    void                setSpeed(Speed speed) noexcept;
    [[nodiscard]] Speed speed() const noexcept;
    /// Call when the simulation starts running.
    void restart(Clock::time_point now) noexcept;
    /// Adds the time since the last call to the debt and says how much of it this tick may pay.
    [[nodiscard]] TickPlan plan(Clock::time_point now) noexcept;
    /// Subtracts the generations actually stepped.
    void commit(std::int64_t generationsDone) noexcept;

private:
    Speed             m_speed{};
    Clock::time_point m_last;
    double            m_owed = 0.0;  ///< Fractional generations owed.
};

/// Achieved generations per second, averaged over windows of at least kWindow.
/// Windows open and close only on batches that stepped, so each one spans whole generations.
class RateMeter
{
public:
    /// A window closes at the first stepped batch at least this long after it opened.
    static constexpr std::chrono::milliseconds kWindow{500};

    void reset() noexcept;  ///< Forgets the measurement; the next stepped batch opens a window.
    /// Reports a batch of `generations` steps that ended at `now`; batches of 0 are ignored.
    void record(std::int64_t generations, Clock::time_point now) noexcept;
    /// Rate of the last complete window; nullopt until one has closed.
    [[nodiscard]] std::optional<double> perSecond() const noexcept;

private:
    std::optional<Clock::time_point> m_windowStart;  ///< End of the batch that opened the window.
    std::int64_t                     m_windowGenerations = 0;  ///< Stepped since m_windowStart.
    std::optional<double>            m_rate;
};

}  // namespace wxLife::core
