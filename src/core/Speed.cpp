#include "wxLife/core/Speed.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace wxLife::core
{

int Speed::fromSliderPosition(int position) noexcept
{
    const int  p    = std::clamp(position, 0, kSliderMax);
    const auto rate = static_cast<int>(std::lround(std::pow(10.0, p / 100.0)));
    return std::clamp(rate, kMin, kMax);
}

int Speed::toSliderPosition(int gensPerSecond) noexcept
{
    const int rate = std::clamp(gensPerSecond, kMin, kMax);
    return static_cast<int>(std::lround(100.0 * std::log10(rate)));
}

std::string toString(Speed speed)
{
    return speed.unlimited ? "Max" : std::format("{} gen/s", speed.gensPerSecond);
}

}  // namespace wxLife::core
