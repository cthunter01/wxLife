#pragma once

#include "wxLife/core/Grid.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// The executable specification: counts all eight neighbours of every cell with explicit edge
/// rules. It ignores ghost cells. Slow on purpose: read it first, and use it as the oracle in
/// tests.
class ReferenceStepper final : public Stepper
{
public:
    /// The UI offers it only up to this size.
    static constexpr CellCount kRecommendedMaxCells = 1'000'000;

    [[nodiscard]] StepperKind kind() const noexcept override { return StepperKind::Reference; }
    CellCount step(const Grid& src, Grid& dst, const Rule& rule, Topology topology) override;
};

}  // namespace wxLife::core
