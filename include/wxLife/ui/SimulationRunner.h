#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <thread>

#include <wx/timer.h>

#include "wxLife/core/HashLife.h"
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
    /// Why an unbounded world could not step; the runner has stopped.
    std::optional<core::HashLifeError> error;
};

/// Steps the World from a one-shot wxTimer at the rate chosen by core::GenerationPacer.
/// After each tick the timer is re-armed with at least kMinIdleGap of idle time: a GLib timeout
/// that is always ready starves GTK's lower-priority redraws (see docs/architecture.md).
///
/// A fixed-size world steps one generation at a time on the UI thread, as many as the tick's time
/// budget allows. An unbounded world steps 2^stepExponent() generations at a time on a worker
/// thread, one step at a time; the timer then only watches for the step to finish. While a step
/// runs, the world may only be read (see core::World); interrupt() or stop() before changing it.
class SimulationRunner
{
public:
    using TickHandler = std::function<void(const TickReport&)>;
    static constexpr std::chrono::milliseconds kTickInterval{16};
    static constexpr std::chrono::milliseconds kMinIdleGap{4};
    /// While a step runs longer than this, the handler hears about it again and again, with no
    /// generations, so the status can say that it is computing.
    static constexpr std::chrono::milliseconds kBusyReport{250};
    /// The largest step the UI offers: 2^40 generations. Larger ones would wrap the generation
    /// counter too soon to be useful.
    static constexpr unsigned kMaxStepExponent = 40;

    /// `world` must outlive this object. `onTick` is called whenever generations were stepped,
    /// by a timer tick or by stepOnce(), and while a long step runs.
    SimulationRunner(core::World& world, TickHandler onTick);
    SimulationRunner(const SimulationRunner&)            = delete;
    SimulationRunner(SimulationRunner&&)                 = delete;
    SimulationRunner& operator=(const SimulationRunner&) = delete;
    SimulationRunner& operator=(SimulationRunner&&)      = delete;
    /// Cancels a running step and waits for it.
    ~SimulationRunner();

    void start();
    /// Safe when already stopped. Cancels a running step and waits for it: the world is then as
    /// before that step.
    void               stop();
    void               toggle();
    [[nodiscard]] bool isRunning() const noexcept;
    /// Pauses first if running, then steps once: one generation, or 2^stepExponent() in an
    /// unbounded world, where the step runs on the worker thread and reports when it is done.
    void stepOnce();
    /// Cancels a running step and waits for it, so the world can be changed; running goes on at
    /// the next tick.
    void                      interrupt();
    void                      setSpeed(core::Speed speed);
    [[nodiscard]] core::Speed speed() const noexcept;
    /// Clamped to [0, kMaxStepExponent]. Fixed-size worlds ignore it.
    void                   setStepExponent(unsigned exponent);
    [[nodiscard]] unsigned stepExponent() const noexcept;
    /// How long the running step has taken so far; nullopt when none runs.
    [[nodiscard]] std::optional<core::Clock::duration> busyFor() const noexcept;
    /// Generations per second over the last window of at least 0.5 s; nullopt until the first
    /// measurement after start().
    [[nodiscard]] std::optional<double> measuredRate() const noexcept;

private:
    void onTimer();
    void scheduleNext(core::Clock::duration tickCost);
    /// A fixed-size world's tick.
    void stepHere();
    /// An unbounded world's tick: collect a finished step, then start the next one if it is due.
    void stepInBackground();
    void launchStep();
    /// Joins a finished step and reports it. @return false if it failed and the runner stopped.
    bool finishStep();
    /// Cancels and joins a running step; its result is dropped.
    void                       cancelStep();
    [[nodiscard]] std::int64_t generationsPerStep() const noexcept;

    core::World&          m_world;
    TickHandler           m_onTick;
    core::GenerationPacer m_pacer;
    core::RateMeter       m_meter;
    wxTimer               m_timer;  ///< Default-constructed: its own owner, so Bind() works.
    bool                  m_running      = false;
    unsigned              m_stepExponent = 0;

    // The background step. The worker writes m_result, then sets m_finished.
    std::thread                              m_worker;
    std::atomic<bool>                        m_cancel{false};
    std::atomic<bool>                        m_finished{false};
    std::expected<void, core::HashLifeError> m_result;
    bool                                     m_stepPending = false;  ///< Started, not collected
    unsigned                                 m_inFlightExponent = 0;
    core::Clock::time_point                  m_stepStarted;
    core::Clock::time_point                  m_lastBusyReport;
    bool                                     m_retried  = false;  ///< After OUT_OF_MEMORY
    bool                                     m_stepOnce = false;  ///< Stop after this step
};

}  // namespace wxLife::ui
