#include "wxLife/ui/SimulationRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#include "wxLife/core/Pacer.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/World.h"

namespace wxLife::ui
{

SimulationRunner::SimulationRunner(core::World& world, TickHandler onTick)
  : m_world(world), m_onTick(std::move(onTick))
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { onTimer(); });
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
    m_timer.StartOnce(static_cast<int>(kTickInterval.count()));
}

void SimulationRunner::stop()
{
    m_running = false;
    m_timer.Stop();
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
    m_world.step();
    if (m_onTick)
    {
        m_onTick({.generationsStepped = 1, .measuredRate = m_meter.perSecond()});
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

std::optional<double> SimulationRunner::measuredRate() const noexcept
{
    return m_meter.perSecond();
}

void SimulationRunner::onTimer()
{
    const auto           start = core::Clock::now();
    const core::TickPlan plan  = m_pacer.plan(start);
    std::int64_t         done  = 0;
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
        m_onTick({.generationsStepped = done, .measuredRate = m_meter.perSecond()});
    }
    if (m_running)  // the handler may have stopped us
    {
        scheduleNext(core::Clock::now() - start);
    }
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
