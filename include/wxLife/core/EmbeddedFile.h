/// @file
/// The pattern files built into the program.
#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace wxLife::core
{

/// A file from patterns/, built into the program by cmake/EmbedPatterns.cmake.
struct EmbeddedFile
{
    std::string_view name;  ///< The file name, e.g. "glider.rle".
    std::string_view text;
};

/// Every file in patterns/ that src/CMakeLists.txt lists, in that order. Defined in a source the
/// build generates.
[[nodiscard]] std::span<const EmbeddedFile> embeddedPatternFiles() noexcept;

/// The text of the embedded file called `name`, or nullopt if there is none.
[[nodiscard]] inline std::optional<std::string_view> embeddedPatternText(
    std::string_view name) noexcept
{
    for (const EmbeddedFile& file : embeddedPatternFiles())
    {
        if (file.name == name)
        {
            return file.text;
        }
    }
    return std::nullopt;
}

}  // namespace wxLife::core
