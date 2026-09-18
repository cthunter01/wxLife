#include "wxLife/core/StepKernel.h"

#include <cstddef>
#include <cstdint>  // also defines __GLIBC__ on glibc, which the test below needs

#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

// target_clones builds the function twice and picks the AVX2 copy at load time (an ELF ifunc) when
// the CPU supports it. The AVX2 copy vectorizes both passes, including the variable shift
// (vpsrlvd). glibc only: musl has no ifunc.
#if defined(WXLIFE_KERNEL_CLONES) && defined(__x86_64__) && defined(__ELF__) && defined(__GLIBC__)
#define WXLIFE_KERNEL_TARGETS [[gnu::target_clones("avx2", "default")]]
#else
#define WXLIFE_KERNEL_TARGETS
#endif

namespace wxLife::core::kernel
{

// Raw pointers keep the loops simple enough for the compiler to vectorize; the callers pass spans'
// data() with the lengths documented in StepKernel.h.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
WXLIFE_KERNEL_TARGETS
std::uint32_t stepRow(const Cell* above, const Cell* row, const Cell* below, Cell* out,
                      std::uint8_t* columnSums, std::size_t width, Rule::KernelMasks masks) noexcept
{
    // Pass 1: vertical 3-sums (0..3), ghost columns included.
    for (std::size_t x = 0; x < width + 2; ++x)
    {
        columnSums[x] = static_cast<std::uint8_t>(above[x] + row[x] + below[x]);
    }

    // Pass 2: the 3×3 sum (0..9) includes the centre cell, and kernelMasks() shifts the survival
    // mask to match.
    std::uint32_t live = 0;
    for (std::size_t x = 1; x <= width; ++x)
    {
        const std::uint32_t sum =
            std::uint32_t{columnSums[x - 1]} + columnSums[x] + columnSums[x + 1];
        const std::uint32_t mask = row[x] != kDead ? masks.alive : masks.dead;
        const auto          next = static_cast<Cell>((mask >> sum) & 1U);
        out[x - 1]               = next;
        live += next;
    }
    return live;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

}  // namespace wxLife::core::kernel
