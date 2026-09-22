#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Why Rule::parse() rejected a text.
enum class RuleError : std::uint8_t
{
    Empty,
    Syntax,
    NeighbourOutOfRange
};

/// Message for the user.
[[nodiscard]] constexpr std::string_view describe(RuleError e) noexcept
{
    switch (e)
    {
        case RuleError::Empty:
            return "Enter a rule such as B3/S23.";
        case RuleError::Syntax:
            return "Rules look like B3/S23: digits after B and S.";
        case RuleError::NeighbourOutOfRange:
            return "Neighbour counts go from 0 to 8.";
    }
    std::unreachable();
}

/// Two-state outer-totalistic rule in B/S notation, e.g. "B3/S23".
class Rule
{
public:
    /// Bit n set = applies to a cell with n live neighbours (n = 0..8).
    using Mask = std::uint16_t;

    /// Masks indexed by the 3×3 sum *including* the centre cell: next = (mask >> sum) & 1.
    struct KernelMasks
    {
        std::uint32_t dead;   ///< == birth mask (a dead centre adds 0)
        std::uint32_t alive;  ///< == survival mask << 1 (a live centre adds 1)
    };

    constexpr Rule() noexcept = default;  ///< Conway's Life, B3/S23.
    /// Bits above 8 are dropped.
    constexpr Rule(Mask birth, Mask survival) noexcept
      : m_birth(static_cast<Mask>(birth & kAllCounts)),
        m_survival(static_cast<Mask>(survival & kAllCounts))
    {
    }

    /// Accepts "B3/S23", "b36/s23", "S23/B3", "B3S23", "B/S", and the legacy survival/birth form
    /// "23/3" (which needs its slash). Letters are case-insensitive; surrounding whitespace is
    /// ignored.
    [[nodiscard]] static constexpr std::expected<Rule, RuleError> parse(
        std::string_view text) noexcept;
    [[nodiscard]] std::string toString() const;  ///< Canonical form, e.g. "B36/S23".

    /// @pre liveNeighbours <= 8
    [[nodiscard]] constexpr Cell nextState(bool alive, unsigned liveNeighbours) const noexcept
    {
        return static_cast<Cell>((unsigned{alive ? m_survival : m_birth} >> liveNeighbours) & 1U);
    }
    [[nodiscard]] constexpr KernelMasks kernelMasks() const noexcept
    {
        return {.dead = m_birth, .alive = static_cast<std::uint32_t>(m_survival) << 1};
    }
    [[nodiscard]] constexpr Mask birthMask() const noexcept { return m_birth; }
    [[nodiscard]] constexpr Mask survivalMask() const noexcept { return m_survival; }

    friend constexpr bool operator==(const Rule&, const Rule&) noexcept = default;

private:
    static constexpr Mask kAllCounts = 0x1FF;

    Mask m_birth    = 1U << 3;
    Mask m_survival = (1U << 2) | (1U << 3);
};

// Defined outside the class: std::expected<Rule, ...> needs the complete Rule. One pass over the
// text keeps the whole grammar in one place, and the tests check it at compile time.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
constexpr std::expected<Rule, RuleError> Rule::parse(std::string_view text) noexcept
{
    constexpr std::string_view kSpace = " \t\n\v\f\r";
    const std::size_t          first  = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos)
    {
        return std::unexpected(RuleError::Empty);
    }
    text = text.substr(first, text.find_last_not_of(kSpace) + 1 - first);

    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    const auto isLetter = [&](char c) { return lower(c) == 'b' || lower(c) == 's'; };
    const bool legacy   = !isLetter(text.front());  // "23/3": survival digits, '/', birth digits

    Mask birth     = 0;
    Mask survival  = 0;
    bool toBirth   = false;  // which mask the next digit goes to
    bool seenB     = false;
    bool seenS     = false;
    bool seenSlash = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = lower(text[i]);
        if (c >= '0' && c <= '8')
        {
            Mask& mask = toBirth ? birth : survival;
            mask       = static_cast<Mask>(unsigned{mask} | (1U << static_cast<unsigned>(c - '0')));
        }
        else if (c == '9')
        {
            return std::unexpected(RuleError::NeighbourOutOfRange);
        }
        else if (c == '/')
        {
            // At most one slash; in the B/S form it may only separate the two parts.
            const bool beforeLetter = i + 1 < text.size() && isLetter(text[i + 1]);
            if (seenSlash || (!legacy && !beforeLetter))
            {
                return std::unexpected(RuleError::Syntax);
            }
            seenSlash = true;
            if (legacy)
            {
                toBirth = true;
            }
        }
        else if (!legacy && c == 'b' && !seenB)
        {
            seenB   = true;
            toBirth = true;
        }
        else if (!legacy && c == 's' && !seenS)
        {
            seenS   = true;
            toBirth = false;
        }
        else
        {
            return std::unexpected(RuleError::Syntax);
        }
    }
    if (legacy && !seenSlash)  // "23" alone is ambiguous
    {
        return std::unexpected(RuleError::Syntax);
    }
    return Rule{birth, survival};
}

/// A rule with its display name, for the preset list.
struct NamedRule
{
    std::string_view name;
    Rule             rule;
};

/// Built-in presets in UI order. `.value()` makes a typo a compile error.
inline constexpr std::array kRulePresets{
    NamedRule{.name = "Conway's Life", .rule = Rule::parse("B3/S23").value()},
    NamedRule{.name = "HighLife", .rule = Rule::parse("B36/S23").value()},
    NamedRule{.name = "Seeds", .rule = Rule::parse("B2/S").value()},
    NamedRule{.name = "Day & Night", .rule = Rule::parse("B3678/S34678").value()},
    NamedRule{.name = "Life without Death", .rule = Rule::parse("B3/S012345678").value()},
    NamedRule{.name = "Maze", .rule = Rule::parse("B3/S12345").value()},
    NamedRule{.name = "2x2", .rule = Rule::parse("B36/S125").value()},
    NamedRule{.name = "Replicator", .rule = Rule::parse("B1357/S1357").value()},
    NamedRule{.name = "Diamoeba", .rule = Rule::parse("B35678/S5678").value()},
    NamedRule{.name = "Morley", .rule = Rule::parse("B368/S245").value()},
};

/// Index into kRulePresets of the preset equal to `rule`, if there is one.
[[nodiscard]] constexpr std::optional<std::size_t> findPreset(const Rule& rule) noexcept
{
    // Indices, not iterators: std::array's iterator is a pointer in libstdc++ and libc++ but a
    // class in MSVC's library, so no one spelling of an iterator variable suits every platform's
    // checks.
    const auto index = static_cast<std::size_t>(std::ranges::distance(
        kRulePresets.begin(), std::ranges::find(kRulePresets, rule, &NamedRule::rule)));
    if (index == kRulePresets.size())
    {
        return std::nullopt;
    }
    return index;
}

}  // namespace wxLife::core
