#include "wxLife/ui/SimulationRunner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#include "wxLife/core/HashLife.h"
#include "wxLife/core/Pacer.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"

namespace wxLife::ui
{

SimulationRunner::SimulationRunner(core::World& world, TickHandler onTick)
  : m_world(world), m_onTick(std::move(onTick))
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { onTimer(); });
}

SimulationRunner::~SimulationRunner()
{
    cancelStep();
}

void SimulationRunner::start()
{
    if (m_running)
    {
        return;
    }
    const auto now = core::Clock::now();
    m_pacer.restart(now);
    m_meter.reset();
    m_running = true;
    if (!m_timer.IsRunning())
    {
        m_timer.StartOnce(static_cast<int>(kTickInterval.count()));
    }
}

void SimulationRunner::stop()
{
    m_running  = false;
    m_stepOnce = false;
    m_timer.Stop();
    cancelStep();
}

void SimulationRunner::toggle()
{
    if (m_running)
    {
        stop();
    }
    else
    {
        start();
    }
}

bool SimulationRunner::isRunning() const noexcept
{
    return m_running;
}

void SimulationRunner::stepOnce()
{
    stop();
    if (m_world.kind() == core::WorldKind::UNBOUNDED)
    {
        // The step may be long, so it runs like any other and reports when it is done.
        m_stepOnce = true;
        launchStep();
        m_timer.StartOnce(static_cast<int>(kTickInterval.count()));
        return;
    }
    m_world.step();
    if (m_onTick)
    {
        m_onTick({.generationsStepped = 1, .measuredRate = m_meter.perSecond(), .error = {}});
    }
}

void SimulationRunner::interrupt()
{
    cancelStep();
    if (m_stepOnce)
    {
        m_stepOnce = false;  // the step the user asked for is gone; nothing else was running
        if (!m_running)
        {
            m_timer.Stop();
        }
    }
}

void SimulationRunner::setSpeed(core::Speed speed)
{
    m_pacer.setSpeed(speed);
}

core::Speed SimulationRunner::speed() const noexcept
{
    return m_pacer.speed();
}

void SimulationRunner::setStepExponent(unsigned exponent)
{
    m_stepExponent = std::min(exponent, kMaxStepExponent);
}

unsigned SimulationRunner::stepExponent() const noexcept
{
    return m_stepExponent;
}

std::optional<core::Clock::duration> SimulationRunner::busyFor() const noexcept
{
    if (!m_stepPending)
    {
        return std::nullopt;
    }
    return core::Clock::now() - m_stepStarted;
}

std::optional<double> SimulationRunner::measuredRate() const noexcept
{
    return m_meter.perSecond();
}

void SimulationRunner::onTimer()
{
    const auto start = core::Clock::now();
    if (m_world.kind() == core::WorldKind::UNBOUNDED || m_stepPending)
    {
        stepInBackground();
    }
    else
    {
        stepHere();
    }
    if (m_running || m_stepPending)  // the handler may have stopped us
    {
        scheduleNext(core::Clock::now() - start);
    }
}

void SimulationRunner::stepHere()
{
    const auto start = core::Clock::now();
    m_pacer.setStepSize(1);
    const core::TickPlan plan = m_pacer.plan(start);
    std::int64_t         done = 0;
    // At least one step per due tick, even when a single step takes longer than the budget.
    while (done < plan.maxGenerations &&
           (done == 0 || core::Clock::now() - start < plan.timeBudget))
    {
        m_world.step();
        ++done;
    }
    m_pacer.commit(done);
    m_meter.record(done, core::Clock::now());
    if (done > 0 && m_onTick)
    {
        m_onTick({.generationsStepped = done, .measuredRate = m_meter.perSecond(), .error = {}});
    }
}

void SimulationRunner::stepInBackground()
{
    const auto now = core::Clock::now();
    if (m_stepPending)
    {
        if (!m_finished.load(std::memory_order_acquire))
        {
            // Still working: say so now and then, so the status can show it.
            if (now - m_stepStarted >= kBusyReport && now - m_lastBusyReport >= kBusyReport)
            {
                m_lastBusyReport = now;
                if (m_onTick)
                {
                    m_onTick({.generationsStepped = 0,
                              .measuredRate       = m_meter.perSecond(),
                              .error              = {}});
                }
            }
            return;
        }
        if (!finishStep() || m_stepPending)  // failed, or retrying after a collection
        {
            return;
        }
        if (m_stepOnce)
        {
            m_stepOnce = false;
            return;
        }
    }
    if (!m_running || m_world.kind() != core::WorldKind::UNBOUNDED)
    {
        return;
    }
    m_pacer.setStepSize(generationsPerStep());
    if (m_pacer.plan(now).maxGenerations >= generationsPerStep())
    {
        launchStep();
    }
}

void SimulationRunner::launchStep()
{
    // Collecting moves nodes, so it happens here, on the UI thread, while no step runs.
    m_world.collectIfFull();
    m_cancel.store(false);
    m_finished.store(false);
    m_stepPending      = true;
    m_stepStarted      = core::Clock::now();
    m_lastBusyReport   = m_stepStarted;
    m_inFlightExponent = m_stepExponent;
    const auto work    = [this, exponent = m_inFlightExponent] {
        m_result = m_world.stepPlane(exponent, {.cancel = &m_cancel, .collectGarbage = false});
        m_finished.store(true, std::memory_order_release);
    };
    try
    {
        m_worker = std::thread(work);
    }
    catch (const std::system_error&)
    {
        work();  // no thread to be had: step here instead, as forEachBand() does
    }
}

bool SimulationRunner::finishStep()
{
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    m_stepPending = false;
    if (m_result)
    {
        m_retried                      = false;
        const std::int64_t generations = std::int64_t{1} << m_inFlightExponent;
        const auto         now         = core::Clock::now();
        m_pacer.commit(generations);
        m_meter.record(generations, now);
        if (m_onTick)
        {
            m_onTick({.generationsStepped = generations,
                      .measuredRate       = m_meter.perSecond(),
                      .error              = {}});
        }
        return true;
    }
    const core::HashLifeError error = m_result.error();
    if (error == core::HashLifeError::CANCELLED)
    {
        return true;  // cancelStep() normally takes these; nothing happened
    }
    if (error == core::HashLifeError::OUT_OF_MEMORY && !m_retried)
    {
        // The worker may not collect garbage; this thread may, now that no step runs.
        m_retried = true;
        m_world.collectGarbage();
        launchStep();
        return true;
    }
    m_retried = false;
    stop();
    if (m_onTick)
    {
        m_onTick({.generationsStepped = 0, .measuredRate = m_meter.perSecond(), .error = error});
    }
    return false;
}

void SimulationRunner::cancelStep()
{
    if (!m_stepPending)
    {
        return;
    }
    m_cancel.store(true);
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    // A step that finished before it saw the flag has already moved the world on; that is fine,
    // the caller only needed it to stop.
    m_stepPending = false;
    m_retried     = false;
    m_cancel.store(false);
}

std::int64_t SimulationRunner::generationsPerStep() const noexcept
{
    return m_world.kind() == core::WorldKind::UNBOUNDED ? std::int64_t{1} << m_stepExponent : 1;
}

// One-shot on purpose: a repeating GLib timeout whose handler outlasts its interval is always
// ready, and GTK, which repaints at a lower priority, then never repaints. The minimum gap leaves
// time for input and paint events after every tick.
void SimulationRunner::scheduleNext(core::Clock::duration tickCost)
{
    using namespace std::chrono;
    const milliseconds delay =
        std::max(kMinIdleGap, kTickInterval - duration_cast<milliseconds>(tickCost));
    m_timer.StartOnce(static_cast<int>(delay.count()));
}

}  // namespace wxLife::ui
