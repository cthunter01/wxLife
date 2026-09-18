#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <wx/timer.h>

#include "wxLife/core/Pacer.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/World.h"

namespace wxLife::ui
{

/// Result of one timer tick.
struct TickReport
{
    std::int64_t          generationsStepped = 0;
    std::optional<double> measuredRate;  ///< As SimulationRunner::measuredRate().
};

/// Steps the World from a one-shot wxTimer at the rate chosen by core::GenerationPacer.
/// After each tick the timer is re-armed with at least kMinIdleGap of idle time: a GLib timeout
/// that is always ready starves GTK's lower-priority redraws (see docs/architecture.md).
class SimulationRunner
{
public:
    using TickHandler = std::function<void(const TickReport&)>;
    static constexpr std::chrono::milliseconds kTickInterval{16};
    static constexpr std::chrono::milliseconds kMinIdleGap{4};

    /// `world` must outlive this object. `onTick` is called whenever generations were stepped,
    /// by a timer tick or by stepOnce().
    SimulationRunner(core::World& world, TickHandler onTick);
    SimulationRunner(const SimulationRunner&)            = delete;
    SimulationRunner(SimulationRunner&&)                 = delete;
    SimulationRunner& operator=(const SimulationRunner&) = delete;
    SimulationRunner& operator=(SimulationRunner&&)      = delete;
    ~SimulationRunner()                                  = default;

    void start();
    /// Safe when already stopped.
    void               stop();
    void               toggle();
    [[nodiscard]] bool isRunning() const noexcept;
    /// Pauses first if running, then steps one generation.
    void                      stepOnce();
    void                      setSpeed(core::Speed speed);
    [[nodiscard]] core::Speed speed() const noexcept;
    /// Generations per second over the last window of at least 0.5 s; nullopt until the first
    /// measurement after start().
    [[nodiscard]] std::optional<double> measuredRate() const noexcept;

private:
    void onTimer();
    void scheduleNext(core::Clock::duration tickCost);

    core::World&          world_;
    TickHandler           onTick_;
    core::GenerationPacer pacer_;
    core::RateMeter       meter_;
    wxTimer               timer_;  ///< Default-constructed: its own owner, so Bind() works.
    bool                  running_ = false;
};

}  // namespace wxLife::ui
