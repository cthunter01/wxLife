#pragma once

#include <cstdint>
#include <limits>

namespace wxLife::core
{

/// SplitMix64: a small, fast, deterministic 64-bit generator. It satisfies
/// std::uniform_random_bit_generator.
class SplitMix64
{
public:
    // The name std::uniform_random_bit_generator requires.
    using result_type = std::uint64_t;  // NOLINT(readability-identifier-naming)

    constexpr explicit SplitMix64(std::uint64_t seed) noexcept : m_state(seed) { }

    static constexpr result_type min() noexcept { return 0; }
    static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }

    constexpr result_type operator()() noexcept
    {
        m_state += 0x9E3779B97F4A7C15U;  // Weyl sequence, then two xor-shift-multiply rounds
        std::uint64_t z = m_state;

        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9U;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBU;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t m_state;
};

}  // namespace wxLife::core
