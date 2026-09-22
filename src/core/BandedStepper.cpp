#include "wxLife/core/BandedStepper.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "wxLife/core/Grid.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/StepKernel.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

BandedStepper::BandedStepper(unsigned maxThreads, CellCount minCellsPerBand) noexcept
  : m_maxThreads(maxThreads), m_minCellsPerBand(minCellsPerBand)
{
}

CellCount BandedStepper::step(const Grid& src, Grid& dst, const Rule& rule, Topology /*topology*/)
{
    // The topology is already in src's ghost border.
    assert(dst.extent() == src.extent());
    const Extent   extent = src.extent();
    const auto     width  = static_cast<std::size_t>(extent.width);
    const unsigned bands  = suggestedBandCount(extent.cellCount(), m_maxThreads, m_minCellsPerBand);

    // Size the scratch here, so the band jobs never allocate. Each band owns one slot of each of
    // the two vectors.
    m_columnSums.resize(bands);
    for (std::vector<std::uint8_t>& sums : m_columnSums)
    {
        sums.resize(width + 2);
    }
    m_bandPopulation.assign(bands, 0);

    const Rule::KernelMasks masks = rule.kernelMasks();
    forEachBand(extent.height, bands, [&](unsigned band, Coord firstRow, Coord endRow) {
        CellCount population = 0;
        for (Coord y = firstRow; y < endRow; ++y)
        {
            population += kernel::stepRow(src.paddedRow(y - 1).data(), src.paddedRow(y).data(),
                                          src.paddedRow(y + 1).data(), dst.row(y).data(),
                                          m_columnSums[band].data(), width, masks);
        }
        m_bandPopulation[band] = population;
    });
    return std::reduce(m_bandPopulation.begin(), m_bandPopulation.end(), CellCount{0});
}

}  // namespace wxLife::core
