/// @file
/// Runs a job over horizontal row bands on short-lived std::jthreads.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <thread>
#include <vector>

#include "wxLife/core/Types.h"

namespace wxLife::core
{

inline constexpr CellCount kMinCellsPerBand = 125'000;  ///< Work worth one band.

/// Bands for work over `cells` cells: one per `minCellsPerBand`, or 1 when that gives fewer than 4.
/// The result is then capped at `maxThreads` (0 = std::thread::hardware_concurrency()).
[[nodiscard]] unsigned suggestedBandCount(CellCount cells, unsigned maxThreads = 0,
                                          CellCount minCellsPerBand = kMinCellsPerBand) noexcept;

/// First row of band `band` when [0, rows) is split into `bands` near-equal parts.
/// bandStart(rows, bands, bands) == rows. @pre bands > 0
[[nodiscard]] constexpr Coord bandStart(Coord rows, unsigned bands, unsigned band) noexcept
{
    return static_cast<Coord>(std::int64_t{rows} * band / bands);
}

/// Calls job(band, firstRow, endRow) for `bands` contiguous half-open row ranges [firstRow, endRow)
/// covering [0, rows); `bands` is clamped to [1, max(rows, 1)]. Band 0 runs on the calling thread
/// and the others on std::jthreads; returns when all have finished. A band whose thread cannot be
/// started (std::system_error, std::bad_alloc) runs on the calling thread.
/// @pre `job` does not throw, and different bands never write the same memory.
template <std::invocable<unsigned, Coord, Coord> Job>
void forEachBand(Coord rows, unsigned bands, Job job)
{
    bands = std::clamp(bands, 1U, static_cast<unsigned>(std::max(rows, 1)));

    const auto runBand = [&](unsigned band) {
        job(band, bandStart(rows, bands, band), bandStart(rows, bands, band + 1));
    };

    std::vector<std::jthread> helpers;  // joined by their destructors when this function returns
    helpers.reserve(bands - 1);
    for (unsigned band = 1; band < bands; ++band)
    {
        try
        {
            helpers.emplace_back(runBand, band);
        }
        catch (...)
        {
            runBand(band);  // no thread, or no memory for its state: do the work here instead
        }
    }
    runBand(0);
}

}  // namespace wxLife::core
