/// @file
/// The built-in demo patterns: what each one is, and the world it runs best in.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "wxLife/core/Ant.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// How the demo list is grouped. The core branches on it only in switches without `default`.
enum class DemoCategory : std::uint8_t
{
    SPACESHIPS,
    GUNS,
    PUFFERS_AND_RAKES,
    BREEDERS,
    METHUSELAHS,
    OSCILLATORS,
    COMPUTATION,
    LANGTONS_ANT,
};

/// Every DemoCategory, in the order the demo list shows them.
inline constexpr std::array kDemoCategories{
    DemoCategory::SPACESHIPS,  DemoCategory::GUNS,        DemoCategory::PUFFERS_AND_RAKES,
    DemoCategory::BREEDERS,    DemoCategory::METHUSELAHS, DemoCategory::OSCILLATORS,
    DemoCategory::COMPUTATION, DemoCategory::LANGTONS_ANT};

[[nodiscard]] constexpr std::string_view toString(DemoCategory c) noexcept
{
    switch (c)
    {
        case DemoCategory::SPACESHIPS:
            return "Spaceships";
        case DemoCategory::GUNS:
            return "Guns";
        case DemoCategory::PUFFERS_AND_RAKES:
            return "Puffers and rakes";
        case DemoCategory::BREEDERS:
            return "Breeders";
        case DemoCategory::METHUSELAHS:
            return "Methuselahs";
        case DemoCategory::OSCILLATORS:
            return "Oscillators";
        case DemoCategory::COMPUTATION:
            return "Computation";
        case DemoCategory::LANGTONS_ANT:
            return "Langton's ant";
    }
    std::unreachable();
}

/// One built-in demo: a pattern, the world it runs best in, and what to watch for. Every setting
/// was chosen by running the demo; the demo tests check that the pattern and its view fit.
struct Demo
{
    // The table in src/core/Demo.cpp leaves out whatever a demo does not need. Clang's
    // -Wmissing-designated-field-initializers then wants a default for every field left out, which
    // readability-redundant-member-init calls redundant for class types.
    // NOLINTBEGIN(readability-redundant-member-init)
    std::string_view name;
    DemoCategory     category = DemoCategory::SPACESHIPS;
    std::string_view credit{};  ///< Who found or built it, and when; may be empty.
    std::string_view about;     ///< What it is and what to watch for.
    /// The embedded pattern file (wxLife/core/EmbeddedFile.h); empty for ants on an empty world.
    std::string_view file{};
    Extent           world;
    Topology         topology = Topology::BOUNDED;
    /// Top-left corner of the pattern in the world; nullopt centres it.
    std::optional<CellPos> origin{};
    Speed                  speed{};
    /// The cells to show first, relative to the pattern's top-left corner; nullopt fits the world.
    std::optional<CellRect> view{};
    Automaton               automaton = Automaton::LIFE;
    std::span<const Ant>    ants{};  ///< Where the ants start; only for Automaton::LANGTON_ANT.
    // NOLINTEND(readability-redundant-member-init)
};

/// Every demo, grouped by category in kDemoCategories order.
[[nodiscard]] std::span<const Demo> demos() noexcept;

/// The demo's pattern, read from its embedded file; an empty pattern for a demo without a file.
/// @pre the file is embedded and readable, which the demo tests check for every demo.
[[nodiscard]] Pattern demoPattern(const Demo& demo);

}  // namespace wxLife::core
