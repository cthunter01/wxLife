#include "wxLife/core/Format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace wxLife::core
{

std::string formatCount(std::uint64_t n)
{
    const std::string digits = std::to_string(n);
    std::string       text;
    text.reserve(digits.size() + (digits.size() / 3));
    for (std::size_t i = 0; i < digits.size(); ++i)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0)
        {
            text += ',';
        }
        text += digits[i];
    }
    return text;
}

std::string formatBytes(std::uint64_t bytes)
{
    if (bytes < 1024)
    {
        return std::format("{} B", bytes);
    }

    static constexpr std::array<std::string_view, 6> kUnits{"KiB", "MiB", "GiB",
                                                            "TiB", "PiB", "EiB"};

    auto        value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit  = 0;
    // Values of 100 and more are printed without a decimal, so 1023.5 would already read "1024".
    while (value >= 1023.5 && unit + 1 < kUnits.size())
    {
        value /= 1024.0;
        ++unit;
    }
    // One decimal below 100 ("1.9 MiB", "15.6 GiB"), none above ("516 KiB").
    return value < 99.95 ? std::format("{:.1f} {}", value, kUnits.at(unit))
                         : std::format("{:.0f} {}", value, kUnits.at(unit));
}

}  // namespace wxLife::core
