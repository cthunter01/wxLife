#pragma once

#include <cstdint>
#include <vector>

#include "wxLife/core/Grid.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// Production stepper: kernel::stepRow over row bands that run in parallel.
class BandedStepper final : public Stepper
{
public:
    /// @param maxThreads 0 = hardware concurrency.
    /// @param minCellsPerBand Tests pass 1 to force many bands.
    explicit BandedStepper(unsigned  maxThreads      = 0,
                           CellCount minCellsPerBand = kMinCellsPerBand) noexcept;

    [[nodiscard]] StepperKind kind() const noexcept override { return StepperKind::BANDED; }
    CellCount step(const Grid& src, Grid& dst, const Rule& rule, Topology topology) override;

private:
    unsigned  m_maxThreads;
    CellCount m_minCellsPerBand;
    /// One scratch row per band, sized before the bands start.
    std::vector<std::vector<std::uint8_t>> m_columnSums;
    std::vector<CellCount>                 m_bandPopulation;
};

}  // namespace wxLife::core
