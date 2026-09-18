#pragma once

#include <cstdint>
#include <string>

namespace wxLife::core
{

/// "1,234,567". The separator is fixed because the C locale has none.
[[nodiscard]] std::string formatCount(std::uint64_t n);
/// "812 B", "1.9 MiB", "15.6 GiB"
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

}  // namespace wxLife::core
