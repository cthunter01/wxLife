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
  : world_(world), onTick_(std::move(onTick))
{
    timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { onTimer(); });
}

void SimulationRunner::start()
{
    if (running_)
    {
        return;
    }
    const auto now = core::Clock::now();
    pacer_.restart(now);
    meter_.reset();
    running_ = true;
    timer_.StartOnce(static_cast<int>(kTickInterval.count()));
}

void SimulationRunner::stop()
{
    running_ = false;
    timer_.Stop();
}

void SimulationRunner::toggle()
{
    if (running_)
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
    return running_;
}

void SimulationRunner::stepOnce()
{
    stop();
    world_.step();
    if (onTick_)
    {
        onTick_({.generationsStepped = 1, .measuredRate = meter_.perSecond()});
    }
}

void SimulationRunner::setSpeed(core::Speed speed)
{
    pacer_.setSpeed(speed);
}

core::Speed SimulationRunner::speed() const noexcept
{
    return pacer_.speed();
}

std::optional<double> SimulationRunner::measuredRate() const noexcept
{
    return meter_.perSecond();
}

void SimulationRunner::onTimer()
{
    const auto           start = core::Clock::now();
    const core::TickPlan plan  = pacer_.plan(start);
    std::int64_t         done  = 0;
    // At least one step per due tick, even when a single step takes longer than the budget.
    while (done < plan.maxGenerations &&
           (done == 0 || core::Clock::now() - start < plan.timeBudget))
    {
        world_.step();
        ++done;
    }
    pacer_.commit(done);
    meter_.record(done, core::Clock::now());
    if (done > 0 && onTick_)
    {
        onTick_({.generationsStepped = done, .measuredRate = meter_.perSecond()});
    }
    if (running_)  // the handler may have stopped us
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
    timer_.StartOnce(static_cast<int>(delay.count()));
}

}  // namespace wxLife::ui
