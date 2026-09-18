#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "wxLife/core/Grid.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// The engines makeStepper() can build.
enum class StepperKind : std::uint8_t
{
    Banded,
    Reference
};

/// Every StepperKind, for menus, command-line parsers and tests.
inline constexpr std::array kStepperKinds{StepperKind::Banded, StepperKind::Reference};

[[nodiscard]] constexpr std::string_view toString(StepperKind kind) noexcept
{
    switch (kind)
    {
        case StepperKind::Banded:
            return "Banded";
        case StepperKind::Reference:
            return "Reference";
    }
    std::unreachable();
}

/// An algorithm that computes the next generation. New engines implement this.
class Stepper
{
public:
    Stepper()                          = default;
    Stepper(const Stepper&)            = delete;
    Stepper(Stepper&&)                 = delete;
    Stepper& operator=(const Stepper&) = delete;
    Stepper& operator=(Stepper&&)      = delete;
    virtual ~Stepper()                 = default;

    [[nodiscard]] virtual StepperKind kind() const noexcept = 0;
    /// Writes the successor of `src` into `dst` and returns its population.
    /// @pre src's ghost border matches `topology` (Grid::updateBorder), and
    ///      dst.extent() == src.extent().
    virtual CellCount step(const Grid& src, Grid& dst, const Rule& rule, Topology topology) = 0;
};

/// The kind whose toString() equals `name`, ignoring ASCII case.
[[nodiscard]] std::optional<StepperKind> parseStepperKind(std::string_view name) noexcept;

/// @param maxThreads Only the Banded engine uses it: at most this many threads,
///                   0 = hardware concurrency.
[[nodiscard]] std::unique_ptr<Stepper> makeStepper(StepperKind kind, unsigned maxThreads = 0);

}  // namespace wxLife::core
