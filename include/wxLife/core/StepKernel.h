/// @file
/// The only hot loop of the simulation, built for AVX2 and baseline x86-64 (runtime dispatch).
#pragma once

#include <cstddef>
#include <cstdint>

#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core::kernel
{

/// Computes one row of the next generation.
/// @param above,row,below ghost-padded input rows, width + 2 cells each
/// @param out             interior output row, width cells
/// @param columnSums      scratch, at least width + 2 bytes
/// @param width           interior cells per row
/// @param masks           Rule::kernelMasks() of the rule to apply
/// @return number of live cells written
std::uint32_t stepRow(const Cell* above, const Cell* row, const Cell* below, Cell* out,
                      std::uint8_t* columnSums, std::size_t width,
                      Rule::KernelMasks masks) noexcept;

}  // namespace wxLife::core::kernel
