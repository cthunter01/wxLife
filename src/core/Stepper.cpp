#include "wxLife/core/Stepper.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "wxLife/core/BandedStepper.h"
#include "wxLife/core/ReferenceStepper.h"

namespace wxLife::core
{

std::optional<StepperKind> parseStepperKind(std::string_view name) noexcept
{
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    const auto sameLetter = [&](char a, char b) { return lower(a) == lower(b); };
    for (const StepperKind kind : kStepperKinds)
    {
        if (std::ranges::equal(name, toString(kind), sameLetter))
        {
            return kind;
        }
    }
    return std::nullopt;
}

std::unique_ptr<Stepper> makeStepper(StepperKind kind, unsigned maxThreads)
{
    switch (kind)
    {
        case StepperKind::BANDED:
            return std::make_unique<BandedStepper>(maxThreads);
        case StepperKind::REFERENCE:
            return std::make_unique<ReferenceStepper>();
    }
    std::unreachable();
}

}  // namespace wxLife::core
