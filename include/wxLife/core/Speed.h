#pragma once

#include <algorithm>
#include <array>
#include <ranges>
#include <string>

namespace wxLife::core
{

/// Target simulation rate: kMin..kMax generations per second, or unlimited ("Max").
struct Speed
{
    static constexpr int kMin       = 1;
    static constexpr int kMax       = 1000;
    static constexpr int kSliderMax = 300;  ///< Log slider: position p ↔ round(10^(p / 100)) gen/s.
    static constexpr std::array kSteps{1, 2, 5, 10, 15, 20, 30, 60, 120, 250, 500, 1000};

    int  gensPerSecond = 30;
    bool unlimited     = false;  ///< gensPerSecond is kept, so leaving Max restores it.

    [[nodiscard]] constexpr Speed clamped() const noexcept
    {
        return {.gensPerSecond = std::clamp(gensPerSecond, kMin, kMax), .unlimited = unlimited};
    }

    /// Next kSteps entry; past the last one, unlimited.
    [[nodiscard]] constexpr Speed faster() const noexcept
    {
        const Speed current = clamped();
        if (current.unlimited)
        {
            return current;
        }
        for (const int step : kSteps)
        {
            if (step > current.gensPerSecond)
            {
                return {.gensPerSecond = step, .unlimited = false};
            }
        }
        return {.gensPerSecond = current.gensPerSecond, .unlimited = true};
    }

    /// Leaves unlimited first, then the previous entry.
    [[nodiscard]] constexpr Speed slower() const noexcept
    {
        const Speed current = clamped();
        if (current.unlimited)
        {
            return {.gensPerSecond = current.gensPerSecond, .unlimited = false};
        }
        for (const int step : kSteps | std::views::reverse)
        {
            if (step < current.gensPerSecond)
            {
                return {.gensPerSecond = step, .unlimited = false};
            }
        }
        return {.gensPerSecond = kMin, .unlimited = false};
    }

    // Not constexpr: std::pow is not constexpr in C++23.
    /// @param position 0..kSliderMax, clamped. @return kMin..kMax
    [[nodiscard]] static int fromSliderPosition(int position) noexcept;
    /// Inverse of fromSliderPosition(), rounded to the nearest position.
    [[nodiscard]] static int toSliderPosition(int gensPerSecond) noexcept;

    friend constexpr bool operator==(Speed, Speed) noexcept = default;
};

[[nodiscard]] std::string toString(Speed speed);  ///< "30 gen/s" or "Max"

}  // namespace wxLife::core
